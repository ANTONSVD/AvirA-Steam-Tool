#include "../src/banparse.h"
#include <cstdio>
#include <string>

static int g_fail = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #cond);               \
            g_fail++;                                                   \
        }                                                               \
    } while (0)

int main() {
    {
        std::string ok = "{\"success\":true,\"transfer_urls\":[],\"transfer_parameters\":{"
                         "\"steamid\":\"76561198012345678\",\"token\":\"ab\",\"auth\":\"cd\","
                         "\"remember_login\":true,\"webcookie\":\"ef\"}}";
        CHECK(ExtractSteamId(ok) == "76561198012345678");
    }
    {
        std::string guard = "{\"success\":false,\"emailauth_needed\":true,"
                            "\"emaildomain\":\"gmail.com\","
                            "\"emailsteamid\":\"76561197987654321\","
                            "\"captcha_needed\":false}";
        CHECK(ExtractSteamId(guard) == "76561197987654321");
    }
    {
        CHECK(ExtractSteamId("{\"success\":false,\"message\":\"no\"}") == "");
        CHECK(ExtractSteamId("") == "");
    }
    {
        std::string vac =
            "<html><body><div class=\"profile_ban_status\">"
            "<div class=\"profile_ban\">1 VAC ban on record"
            "<span class=\"profile_ban_info\">| 1234 day(s) since last ban</span>"
            "</div></div><div class=\"responsive_count_link_area\"></div></body></html>";
        BanInfo bi = ParseBanHtml(vac);
        CHECK(bi.banned);
        CHECK(bi.text == "VAC BAN");
        CHECK(bi.days == 1234);
    }
    {
        std::string game =
            "<div class=\"profile_ban_status\"><div class=\"profile_ban\">"
            "2 game bans on record | <span>5 day(s) since last ban</span>"
            "</div></div><div class=\"responsive_count_link_area\">";
        BanInfo bi = ParseBanHtml(game);
        CHECK(bi.banned);
        CHECK(bi.text == "GAME BAN");
        CHECK(bi.days == 5);
    }
    {
        std::string both =
            "<div class=\"profile_ban_status\">1 VAC ban on record, "
            "1 game ban on record</div><div class=\"responsive_count_link_area\">";
        BanInfo bi = ParseBanHtml(both);
        CHECK(bi.banned);
        CHECK(bi.text == "VAC + GAME BAN");
    }
    {
        std::string clean =
            "<html><body><div class=\"profile_header\"></div>"
            "<div class=\"responsive_count_link_area\"></div></body></html>";
        CHECK(!ParseBanHtml(clean).banned);
        CHECK(ParseBanHtml("").banned == false);
    }
    {
        std::string nodays =
            "<div class=\"profile_ban_status\">3 VAC bans on record</div>"
            "<div class=\"responsive_count_link_area\">";
        BanInfo bi = ParseBanHtml(nodays);
        CHECK(bi.banned);
        CHECK(bi.text == "VAC BAN");
        CHECK(bi.days == 0);
    }
    {
        std::string live =
            "<div class=\"profile_ban_status\">\n"
            "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t<div class=\"profile_ban\">\n"
            "\t\t\t\t\t1 game ban on record\t\t\t\t\t"
            "<span class=\"profile_ban_info\">| <a class=\"whiteLink\" "
            "href=\"https://support.steampowered.com/kb_article.php?"
            "ref=6899-IOSK-9514&l=english\" target=\"_blank\" rel=\"\" >"
            "Info</a></span>\n"
            "\t\t\t\t</div>\n"
            "\t\t\t\t\t\t3225 day(s) since last ban"
            "\t\t\t\t\t\t\t\t</div>\n"
            "\t\t\t</div>\n"
            "\t\t</div>\n"
            "\t</div>\n"
            "</div>\n"
            "\n\t\t</div>\t<!-- responsive_page_legacy_content -->";
        BanInfo bi = ParseBanHtml(live);
        CHECK(bi.banned);
        CHECK(bi.text == "GAME BAN");
        CHECK(bi.days == 3225);
    }

    if (g_fail == 0) printf("banparse: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
