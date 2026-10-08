#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <chrono>

namespace steamtoken {

inline bool LooksLikeJwt(const std::string& s) {
    if (s.size() < 64 || s.size() > 8192) return false;
    if (s.size() < 3 || s[0] != 'e' || s[1] != 'y') return false;
    int dots = 0;
    for (char c : s) {
        if (c == '.') {
            dots++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '+' || c == '/' ||
                     c == '-' || c == '_' || c == '=')) {
            return false;
        }
    }
    return dots == 2;
}

inline bool Base64UrlDecode(const std::string& in, std::string& out) {
    static int tbl[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) tbl[i] = -1;
        const char* ab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; ab[i]; i++) tbl[(unsigned char)ab[i]] = i;
        tbl[(unsigned char)'-'] = 62;
        tbl[(unsigned char)'_'] = 63;
        init = true;
    }
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (char ch : in) {
        unsigned char c = (unsigned char)ch;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=') continue;
        int v = tbl[c];
        if (v < 0) return false;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xFF);
        }
    }
    return !out.empty();
}

inline std::string JsonString(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return "";
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) p++;
    if (p >= j.size() || j[p] != '"') return "";
    p++;
    std::string out;
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) {
            out += j[p + 1];
            p += 2;
            continue;
        }
        out += j[p++];
    }
    return out;
}

inline bool JsonUint(const std::string& j, const std::string& key, unsigned long long& v) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) p++;
    if (p >= j.size() || j[p] < '0' || j[p] > '9') return false;
    v = 0;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') {
        v = v * 10 + (unsigned)(j[p] - '0');
        p++;
    }
    return true;
}

inline unsigned long long NowUnix() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct JwtClaims {
    bool ok = false;
    bool expired = true;
    std::string sub;
    std::string iss;
    unsigned long long exp = 0;
    bool audClient = false;
};

inline JwtClaims ParseJwt(const std::string& token) {
    JwtClaims c;
    size_t d1 = token.find('.');
    if (d1 == std::string::npos) return c;
    size_t d2 = token.find('.', d1 + 1);
    if (d2 == std::string::npos || token.find('.', d2 + 1) != std::string::npos) return c;
    std::string payload;
    if (!Base64UrlDecode(token.substr(d1 + 1, d2 - d1 - 1), payload)) return c;
    c.iss = JsonString(payload, "iss");
    c.sub = JsonString(payload, "sub");
    unsigned long long exp = 0;
    bool hasExp = JsonUint(payload, "exp", exp);
    c.exp = exp;
    size_t ap = payload.find("\"aud\"");
    if (ap != std::string::npos) {
        size_t colon = payload.find(':', ap + 5);
        if (colon != std::string::npos) {
            size_t q = colon + 1;
            while (q < payload.size() && (payload[q] == ' ' || payload[q] == '\t')) q++;
            if (q < payload.size() && payload[q] == '[') {
                size_t end = payload.find(']', q);
                if (end != std::string::npos &&
                    payload.find("\"client\"", q) < end)
                    c.audClient = true;
            } else if (q < payload.size() && payload[q] == '"') {
                c.audClient = payload.compare(q + 1, 6, "client") == 0;
            }
        }
    }
    bool subOk = c.sub.size() == 17;
    for (char ch : c.sub) {
        if (ch < '0' || ch > '9') { subOk = false; break; }
    }
    c.ok = (c.iss == "steam") && subOk && c.audClient;
    if (hasExp) c.expired = exp <= NowUnix();
    return c;
}

inline uint32_t Crc32(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

inline std::string CrcKey(const std::string& accountName) {
    std::string low = accountName;
    for (auto& c : low)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    char buf[16];
    snprintf(buf, sizeof(buf), "%x", Crc32(low));
    return std::string(buf) + "1";
}

struct VdfBlock {
    std::string key;
    size_t open = 0;
    size_t close = 0;
};

inline size_t MatchBrace(const std::string& t, size_t open) {
    int depth = 0;
    bool instr = false;
    for (size_t i = open; i < t.size(); i++) {
        char c = t[i];
        if (c == '"') {
            instr = !instr;
        } else if (!instr) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) return i;
            }
        }
    }
    return std::string::npos;
}

inline bool ReadQuoted(const std::string& t, size_t& pos, std::string& val) {
    while (pos < t.size() && (t[pos] == ' ' || t[pos] == '\t' ||
                              t[pos] == '\r' || t[pos] == '\n')) pos++;
    if (pos >= t.size() || t[pos] != '"') return false;
    pos++;
    val.clear();
    while (pos < t.size() && t[pos] != '"') val += t[pos++];
    if (pos >= t.size()) return false;
    pos++;
    return true;
}

inline std::vector<VdfBlock> ChildBlocks(const std::string& t, size_t open, size_t close) {
    std::vector<VdfBlock> out;
    size_t pos = open + 1;
    while (pos < close) {
        size_t save = pos;
        std::string key;
        if (!ReadQuoted(t, pos, key)) {
            pos = save + 1;
            continue;
        }
        size_t p = pos;
        while (p < close && (t[p] == ' ' || t[p] == '\t' ||
                             t[p] == '\r' || t[p] == '\n')) p++;
        if (p < close && t[p] == '{') {
            size_t end = MatchBrace(t, p);
            if (end == std::string::npos || end > close) {
                pos = save + 1;
                continue;
            }
            out.push_back({key, p, end});
            pos = end + 1;
        }
    }
    return out;
}

inline std::string BlockKey(const std::string& t, size_t open) {
    size_t p = open;
    while (p > 0 && (t[p - 1] == ' ' || t[p - 1] == '\t' ||
                     t[p - 1] == '\r' || t[p - 1] == '\n')) p--;
    if (p == 0 || t[p - 1] != '"') return "";
    size_t e = p - 1;
    size_t s = e;
    while (s > 0 && t[s - 1] != '"') s--;
    return t.substr(s, e - s);
}

inline bool FindDeep(const std::string& t, size_t open, size_t close,
                     const std::string& key, VdfBlock& out) {
    for (auto& b : ChildBlocks(t, open, close)) {
        if (b.key == key) {
            out = b;
            return true;
        }
        if (FindDeep(t, b.open, b.close, key, out)) return true;
    }
    return false;
}

inline bool SetChildValue(std::string& t, size_t open, size_t close,
                          const std::string& key, const std::string& val) {
    size_t pos = open + 1;
    while (pos < close) {
        size_t save = pos;
        std::string k;
        if (!ReadQuoted(t, pos, k)) {
            pos = save + 1;
            continue;
        }
        size_t p = pos;
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t' ||
                                t[p] == '\r' || t[p] == '\n')) p++;
        if (p < t.size() && t[p] == '{') {
            size_t end = MatchBrace(t, p);
            pos = (end == std::string::npos) ? save + 1 : end + 1;
            continue;
        }
        if (k == key && p < t.size() && t[p] == '"') {
            size_t v0 = p + 1;
            size_t v1 = t.find('"', v0);
            if (v1 == std::string::npos || v1 > close) return false;
            t.replace(v0, v1 - v0, val);
            return true;
        }
        if (p < t.size() && t[p] == '"') {
            size_t v1 = t.find('"', p + 1);
            pos = (v1 == std::string::npos) ? save + 1 : v1 + 1;
        }
    }
    std::string ins = "\n\t\t\"" + key + "\"\t\t\"" + val + "\"";
    if (close > t.size()) return false;
    t.insert(close, ins);
    return true;
}

inline bool PatchLocalVdf(std::string& t, const std::string& ckKey,
                          const std::string& blobHex) {
    if (t.empty() || ckKey.empty() || blobHex.empty()) return false;
    size_t root = t.find('{');
    if (root == std::string::npos) return false;
    size_t rend = MatchBrace(t, root);
    if (rend == std::string::npos) return false;
    VdfBlock cc;
    if (!FindDeep(t, root, rend, "ConnectCache", cc)) return false;
    return SetChildValue(t, cc.open, cc.close, ckKey, blobHex);
}

inline bool PatchConfigVdf(std::string& t, const std::string& name,
                           const std::string& sid) {
    if (t.empty() || name.empty() || sid.empty()) return false;
    size_t root = t.find('{');
    if (root == std::string::npos) return false;
    size_t rend = MatchBrace(t, root);
    if (rend == std::string::npos) return false;
    VdfBlock acc;
    if (!FindDeep(t, root, rend, "Accounts", acc)) return false;
    std::string lowName = name;
    for (auto& c : lowName)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    bool hit = false;
    for (auto& b : ChildBlocks(t, acc.open, acc.close)) {
        std::string low = b.key;
        for (auto& c : low)
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (low == lowName) {
            if (!SetChildValue(t, b.open, b.close, "SteamID", sid)) return false;
            hit = true;
            root = t.find('{');
            rend = MatchBrace(t, root);
            if (rend == std::string::npos) return false;
            if (!FindDeep(t, root, rend, "Accounts", acc)) return false;
            break;
        }
    }
    if (!hit) {
        std::string ins = "\n\t\t\t\"" + name + "\"\n\t\t\t{\n\t\t\t\t\"SteamID\"\t\t\"" +
                          sid + "\"\n\t\t\t}";
        if (acc.close > t.size()) return false;
        t.insert(acc.close, ins);
        root = t.find('{');
        rend = MatchBrace(t, root);
        if (rend == std::string::npos) return false;
    }
    size_t ap = t.find("\"AlwaysShowUserChooser\"");
    if (ap != std::string::npos && ap < rend) {
        size_t q = t.find('"', ap + 24);
        if (q == std::string::npos) return false;
        size_t v0 = q + 1;
        size_t v1 = t.find('"', v0);
        if (v1 == std::string::npos) return false;
        t.replace(v0, v1 - v0, "0");
        return true;
    }
    t.insert(rend, "\n\t\"AlwaysShowUserChooser\"\t\t\"0\"");
    return true;
}

inline bool FindUsersBlock(const std::string& t, VdfBlock& users) {
    size_t root = t.find('{');
    if (root == std::string::npos) return false;
    size_t rend = MatchBrace(t, root);
    if (rend == std::string::npos) return false;
    if (BlockKey(t, root) == "users") {
        users.key = "users";
        users.open = root;
        users.close = rend;
        return true;
    }
    return FindDeep(t, root, rend, "users", users);
}

inline bool PatchLoginUsersVdf(std::string& t, const std::string& name,
                               const std::string& sid,
                               const std::string& timestamp) {
    if (t.empty() || name.empty() || sid.empty()) return false;
    std::string lowName = name;
    for (auto& c : lowName)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    VdfBlock users;
    if (!FindUsersBlock(t, users)) return false;
    auto zeroMostRecent = [&](VdfBlock b) {
        size_t pos = b.open + 1;
        while (pos < b.close) {
            size_t save = pos;
            std::string k;
            if (!ReadQuoted(t, pos, k)) {
                pos = save + 1;
                continue;
            }
            size_t p = pos;
            while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
            if (p < t.size() && t[p] == '{') {
                size_t end = MatchBrace(t, p);
                pos = (end == std::string::npos) ? save + 1 : end + 1;
                continue;
            }
            if (k == "MostRecent" && p < t.size() && t[p] == '"') {
                size_t v0 = p + 1;
                size_t v1 = t.find('"', v0);
                if (v1 != std::string::npos && v1 < b.close && (v1 - v0) == 1)
                    t.replace(v0, 1, "0");
                pos = v1 == std::string::npos ? save + 1 : v1 + 1;
                continue;
            }
            if (p < t.size() && t[p] == '"') {
                size_t v1 = t.find('"', p + 1);
                pos = (v1 == std::string::npos) ? save + 1 : v1 + 1;
            }
        }
    };
    for (auto& b : ChildBlocks(t, users.open, users.close)) zeroMostRecent(b);
    if (!FindUsersBlock(t, users)) return false;
    std::string targetKey;
    for (auto& b : ChildBlocks(t, users.open, users.close)) {
        size_t pos = b.open + 1;
        while (pos < b.close) {
            size_t save = pos;
            std::string k;
            if (!ReadQuoted(t, pos, k)) {
                pos = save + 1;
                continue;
            }
            size_t p = pos;
            while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
            if (p < t.size() && t[p] == '{') {
                size_t end = MatchBrace(t, p);
                pos = (end == std::string::npos) ? save + 1 : end + 1;
                continue;
            }
            if (k == "AccountName" && p < t.size() && t[p] == '"') {
                size_t v0 = p + 1;
                size_t v1 = t.find('"', v0);
                if (v1 == std::string::npos || v1 > b.close) break;
                std::string cur = t.substr(v0, v1 - v0);
                for (auto& c : cur)
                    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                if (cur == lowName) targetKey = b.key;
                break;
            }
            if (p < t.size() && t[p] == '"') {
                size_t v1 = t.find('"', p + 1);
                pos = (v1 == std::string::npos) ? save + 1 : v1 + 1;
            }
        }
        if (!targetKey.empty()) break;
    }
    if (!targetKey.empty()) {
        const char* keys[5] = {"MostRecent", "RememberPassword", "AllowAutoLogin",
                               "SteamID", "Timestamp"};
        std::string vals[5] = {"1", "1", "1", sid, timestamp};
        for (int i = 0; i < 5; i++) {
            if (!FindUsersBlock(t, users)) return false;
            bool done = false;
            for (auto& b : ChildBlocks(t, users.open, users.close)) {
                if (b.key != targetKey) continue;
                if (!SetChildValue(t, b.open, b.close, keys[i], vals[i])) return false;
                done = true;
                break;
            }
            if (!done) return false;
        }
        return true;
    }
    std::string ins = "\n\t\"" + sid + "\"\n\t{\n\t\t\"AccountName\"\t\t\"" + name +
                      "\"\n\t\t\"PersonaName\"\t\t\"" + name +
                      "\"\n\t\t\"RememberPassword\"\t\t\"1\"\n\t\t\"MostRecent\"\t\t\"1\"\n\t\t\"AllowAutoLogin\"\t\t\"1\"\n\t\t\"Timestamp\"\t\t\"" +
                      timestamp + "\"\n\t\t\"SteamID\"\t\t\"" + sid + "\"\n\t}";
    if (users.close > t.size()) return false;
    t.insert(users.close, ins);
    return true;
}

}
