#pragma once
#include "util.h"
#include <string>
#include <cctype>
#include <cstdlib>

struct BanInfo {
    std::string text;
    int days = 0;
    bool banned = false;
};

inline std::string ExtractSteamId(const std::string& body) {
    const char* keys[] = {"\"steamid\"", "\"emailsteamid\""};
    for (int k = 0; k < 2; k++) {
        size_t p = body.find(keys[k]);
        if (p == std::string::npos) continue;
        p = body.find(':', p);
        if (p == std::string::npos) continue;
        p++;
        while (p < body.size() && (body[p] == ' ' || body[p] == '"')) p++;
        size_t e = p;
        while (e < body.size() && isdigit((unsigned char)body[e])) e++;
        if (e - p >= 10) return body.substr(p, e - p);
    }
    size_t p = body.find("7656119");
    if (p != std::string::npos) {
        size_t e = p;
        while (e < body.size() && isdigit((unsigned char)body[e])) e++;
        if (e - p == 17) return body.substr(p, 17);
    }
    return "";
}

inline BanInfo ParseBanHtml(const std::string& htmlOrig) {
    BanInfo bi;
    std::string html = util::ToLower(htmlOrig);
    size_t blk = html.find("profile_ban_status");
    if (blk == std::string::npos) return bi;
    size_t blkEnd = html.find("responsive_count_link_area", blk);
    if (blkEnd == std::string::npos) blkEnd = html.size();
    std::string sec = html.substr(blk, blkEnd - blk);

    bool vac = sec.find("vac ban") != std::string::npos;
    bool game = sec.find("game ban") != std::string::npos;
    if (!vac && !game) return bi;

    size_t daysPos = sec.find("day(s) since last ban");
    if (daysPos == std::string::npos)
        daysPos = sec.find("days since last ban");
    if (daysPos != std::string::npos) {
        size_t numEnd = daysPos;
        while (numEnd > 0 && sec[numEnd - 1] == ' ') numEnd--;
        size_t numStart = numEnd;
        while (numStart > 0 && isdigit((unsigned char)sec[numStart - 1])) numStart--;
        if (numStart < numEnd)
            bi.days = atoi(sec.substr(numStart, numEnd - numStart).c_str());
    }

    if (vac && game) bi.text = "VAC + GAME BAN";
    else if (vac) bi.text = "VAC BAN";
    else bi.text = "GAME BAN";
    bi.banned = true;
    return bi;
}
