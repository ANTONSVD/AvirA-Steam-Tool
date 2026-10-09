#pragma once
#include <string>
#include <vector>
#include <random>

namespace signup {

inline std::string GenLogin(std::mt19937& rng) {
    static const char* pre[] = {"vex", "mor", "kai", "zex", "lun", "dra", "nyx",
                                "pex", "quil", "rav", "syx", "tor", "umbr", "vex",
                                "wex", "xyl", "yex", "zor"};
    static const char* mid[] = {"ar", "en", "il", "or", "un", "ax", "eth",
                                "ost", "im", "ash", "ur", "el"};
    static const char* suf[] = {"a", "o", "i", "er", "or", "ax", "is", "on"};
    std::string s = pre[rng() % 18];
    s += mid[rng() % 12];
    if (rng() % 2) s += suf[rng() % 8];
    int digits = 2 + (int)(rng() % 3);
    for (int i = 0; i < digits; i++) s += (char)('0' + rng() % 10);
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    if (out.size() < 5) out += "77";
    return out;
}

inline std::string GenPassword(std::mt19937& rng) {
    static const char* sets[] = {"ABCDEFGHJKLMNPQRSTUVWXYZ", "abcdefghijkmnpqrstuvwxyz",
                                 "23456789", "!@#$%^&*-_+="};
    std::string s;
    for (int k = 0; k < 4; k++) {
        const char* set = sets[k];
        size_t n = 0;
        while (set[n]) n++;
        s += set[rng() % n];
    }
    for (int i = 0; i < 10; i++) {
        const char* set = sets[rng() % 4];
        size_t n = 0;
        while (set[n]) n++;
        s += set[rng() % n];
    }
    for (size_t i = s.size() - 1; i > 0; i--) {
        size_t j = rng() % (i + 1);
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
    return s;
}

inline std::string GenMailLogin(std::mt19937& rng) {
    std::string s = "avira";
    for (int i = 0; i < 8; i++) {
        int v = (int)(rng() % 36);
        s += (char)(v < 10 ? '0' + v : 'a' + v - 10);
    }
    return s;
}

inline std::string ToLowerAscii(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

inline bool LooksSteamMail(const std::string& from, const std::string& subject) {
    std::string f = ToLowerAscii(from + " " + subject);
    return f.find("steam") != std::string::npos;
}

struct MailMsg {
    std::string id;
    std::string from;
    std::string subject;
};

inline std::string JsonStrAt(const std::string& j, size_t from, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat, from);
    if (p == std::string::npos) return "";
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) p++;
    if (p >= j.size()) return "";
    if (j[p] == '"') {
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
    if (j[p] == '{') {
        std::string addr = JsonStrAt(j, p, "address");
        if (!addr.empty()) return addr;
        return JsonStrAt(j, p, "name");
    }
    if (j[p] == '[') {
        size_t e = j.find(']', p);
        if (e == std::string::npos) return "";
        std::string inner = j.substr(p, e - p);
        std::string s = JsonStrAt(inner, 0, "address");
        if (!s.empty()) return s;
        s = JsonStrAt(inner, 0, "name");
        if (!s.empty()) return s;
        size_t q = inner.find('"');
        if (q == std::string::npos) return "";
        q++;
        std::string out;
        while (q < inner.size() && inner[q] != '"') {
            if (inner[q] == '\\' && q + 1 < inner.size()) {
                out += inner[q + 1];
                q += 2;
                continue;
            }
            out += inner[q++];
        }
        return out;
    }
    return "";
}

inline std::vector<MailMsg> ScanMessages(const std::string& body) {
    std::vector<MailMsg> out;
    size_t pos = 0;
    while (true) {
        size_t p = body.find("\"id\"", pos);
        if (p == std::string::npos) break;
        size_t c = body.find(':', p + 4);
        if (c == std::string::npos) break;
        c++;
        while (c < body.size() && (body[c] == ' ' || body[c] == '\t')) c++;
        MailMsg m;
        if (c < body.size() && body[c] == '"') {
            c++;
            while (c < body.size() && body[c] != '"') {
                if (body[c] == '\\' && c + 1 < body.size()) {
                    m.id += body[c + 1];
                    c += 2;
                    continue;
                }
                m.id += body[c++];
            }
        } else {
            while (c < body.size() && body[c] >= '0' && body[c] <= '9') m.id += body[c++];
        }
        if (m.id.empty()) {
            pos = c + 1;
            continue;
        }
        size_t next = body.find("\"id\"", c);
        size_t scope = next == std::string::npos ? body.size() : next;
        std::string slice = body.substr(c, scope - c);
        m.from = JsonStrAt(slice, 0, "from");
        m.subject = JsonStrAt(slice, 0, "subject");
        out.push_back(m);
        pos = c;
    }
    return out;
}

inline std::string StripTags(const std::string& h) {
    std::string out;
    bool tag = false;
    for (size_t i = 0; i < h.size(); i++) {
        if (h[i] == '<') {
            tag = true;
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        if (h[i] == '>') {
            tag = false;
            continue;
        }
        if (!tag) out += h[i];
    }
    return out;
}

inline std::string HtmlUnescape(std::string s) {
    const char* from[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&#39;", "&nbsp;"};
    const char* to[] = {"&", "<", ">", "\"", "'", " "};
    for (int k = 0; k < 6; k++) {
        size_t p = 0;
        size_t fl = 0;
        while (from[k][fl]) fl++;
        while ((p = s.find(from[k], p)) != std::string::npos) {
            s.replace(p, fl, to[k]);
            p += 1;
        }
    }
    return s;
}

inline std::string MessageText(const std::string& body) {
    std::string t = JsonStrAt(body, 0, "text");
    if (!t.empty()) return t;
    t = JsonStrAt(body, 0, "html");
    if (!t.empty()) return HtmlUnescape(StripTags(t));
    t = JsonStrAt(body, 0, "body");
    if (!t.empty()) {
        if (t.find('<') != std::string::npos && t.find('>') != std::string::npos)
            return HtmlUnescape(StripTags(t));
        return t;
    }
    return "";
}

inline std::string ExtractVerifyLink(const std::string& text) {
    static const char* prefixes[] = {"https://store.steampowered.com/join/",
                                     "https://store.steampowered.com/account/verify"};
    for (auto* pre : prefixes) {
        size_t p = 0;
        while ((p = text.find(pre, p)) != std::string::npos) {
            size_t e = p;
            while (e < text.size() && text[e] != ' ' && text[e] != '\t' &&
                   text[e] != '\r' && text[e] != '\n' && text[e] != '"' &&
                   text[e] != '\'' && text[e] != '<' && text[e] != '>' &&
                   text[e] != '\\')
                e++;
            std::string url = HtmlUnescape(text.substr(p, e - p));
            while (!url.empty() && (url.back() == '.' || url.back() == ',' ||
                                    url.back() == ')' || url.back() == ';' ||
                                    url.back() == '\\'))
                url.pop_back();
            if (url.size() > 40) return url;
            p = e;
        }
    }
    return "";
}

inline std::string CaptchaPageHtml(const std::string& sitekey) {
    std::string h =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>AvirA captcha</title>"
        "<script src=\"https://js.hcaptcha.com/1/api.js?render=explicit&hl=ru\" async defer></script>"
        "</head><body style=\"background:#14141f;color:#eee;font-family:sans-serif;padding:24px\">"
        "<h3>AvirA: реши капчу, токен скопируется сам</h3>"
        "<div id=\"cap\"></div><br>"
        "<textarea id=\"tok\" rows=\"5\" cols=\"90\" readonly "
        "placeholder=\"токен появится здесь — вставь его в AvirA\"></textarea>"
        "<script>var w=null;"
        "function go(){try{w=hcaptcha.render(\"cap\",{sitekey:\"SITEKEY\",theme:\"dark\","
        "callback:function(t){var a=document.getElementById(\"tok\");a.value=t;a.select();"
        "try{document.execCommand(\"copy\");}catch(e){}}});}catch(e){setTimeout(go,500);}}"
        "var i=setInterval(function(){if(typeof hcaptcha!==\"undefined\"){clearInterval(i);go();}},200);"
        "</script></body></html>";
    size_t p = h.find("SITEKEY");
    if (p != std::string::npos) h.replace(p, 7, sitekey);
    return h;
}

}
