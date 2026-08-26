#pragma once
#include <string>

namespace jsonmini {

inline std::string GetString(const std::string& j, const std::string& key) {
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
    while (p < j.size()) {
        char c = j[p];
        if (c == '\\' && p + 1 < j.size()) {
            char n = j[p + 1];
            switch (n) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                default: out += n; break;
            }
            p += 2;
            continue;
        }
        if (c == '"') break;
        out += c;
        p++;
    }
    return out;
}

inline bool GetBool(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return false;
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < j.size() && (j[p] == ' ')) p++;
    return j.compare(p, 4, "true") == 0;
}

}
