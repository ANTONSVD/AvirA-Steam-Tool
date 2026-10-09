#include "../src/steamtoken.h"
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

static std::string B64(const std::string& s) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < s.size(); i += 3) {
        unsigned v = (unsigned char)s[i] << 16;
        if (i + 1 < s.size()) v |= (unsigned char)s[i + 1] << 8;
        if (i + 2 < s.size()) v |= (unsigned char)s[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < s.size()) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < s.size()) ? tbl[v & 63] : '=';
    }
    return out;
}

static std::string MakeJwt(const std::string& payload) {
    return B64("{\"typ\":\"JWT\",\"alg\":\"EdDSA\"}") + "." + B64(payload) + "." + B64("sig");
}

static uint32_t RefCrc(const std::string& s) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            t[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) crc = t[(crc ^ c) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

int main() {
    std::string good = MakeJwt("{ \"iss\": \"steam\", \"sub\": \"76561199086309324\", "
                               "\"aud\": [ \"client\", \"web\", \"renew\", \"derive\" ], "
                               "\"exp\": 4102444800, \"iat\": 1783106954 }");
    CHECK(steamtoken::LooksLikeJwt(good));
    CHECK(!steamtoken::LooksLikeJwt("someuser123:MyStr0ngPass!"));
    CHECK(!steamtoken::LooksLikeJwt("eyJhbGciOiJFZERTQSJ9"));
    CHECK(!steamtoken::LooksLikeJwt(""));
    CHECK(!steamtoken::LooksLikeJwt("a.b.c"));
    CHECK(!steamtoken::LooksLikeJwt("user----pass"));

    steamtoken::JwtClaims jc = steamtoken::ParseJwt(good);
    CHECK(jc.ok);
    CHECK(!jc.expired);
    CHECK(jc.sub == "76561199086309324");
    CHECK(jc.iss == "steam");
    CHECK(jc.exp == 4102444800ull);
    CHECK(jc.audClient);

    steamtoken::JwtClaims old = steamtoken::ParseJwt(
        MakeJwt("{\"iss\":\"steam\",\"sub\":\"76561199086309324\","
                "\"aud\":[\"client\"],\"exp\":100}"));
    CHECK(old.ok);
    CHECK(old.expired);

    CHECK(!steamtoken::ParseJwt(
        MakeJwt("{\"iss\":\"evil\",\"sub\":\"76561199086309324\","
                "\"aud\":[\"client\"],\"exp\":4102444800}")).ok);
    CHECK(!steamtoken::ParseJwt(
        MakeJwt("{\"iss\":\"steam\",\"sub\":\"76561199086309324\","
                "\"aud\":[\"mobile\"],\"exp\":4102444800}")).ok);
    CHECK(!steamtoken::ParseJwt(
        MakeJwt("{\"iss\":\"steam\",\"sub\":\"notanid\","
                "\"aud\":[\"client\"],\"exp\":4102444800}")).ok);
    CHECK(!steamtoken::ParseJwt("eyJ.nope").ok);
    CHECK(!steamtoken::ParseJwt(good + ".extra").ok);

    CHECK(steamtoken::Crc32("123456789") == 0xCBF43926u);
    CHECK(steamtoken::Crc32("someuser123") == RefCrc("someuser123"));
    CHECK(steamtoken::Crc32("Another_Acc-99") == RefCrc("Another_Acc-99"));
    CHECK(steamtoken::Crc32("") == RefCrc(""));
    std::string k1 = steamtoken::CrcKey("SomeUser123");
    std::string k2 = steamtoken::CrcKey("someuser123");
    CHECK(k1 == k2);
    CHECK(!k1.empty() && k1.back() == '1');
    for (char c : k1) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '1');

    {
        std::string local = "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n"
                            "\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"ConnectCache\"\n\t\t\t\t{\n"
                            "\t\t\t\t\t\"aabbccdd1\"\t\t\"001122\"\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
        CHECK(steamtoken::PatchLocalVdf(local, "aabbccdd1", "ffeedd"));
        CHECK(local.find("\"aabbccdd1\"\t\t\"ffeedd\"") != std::string::npos);
        CHECK(local.find("001122") == std::string::npos);
        CHECK(steamtoken::PatchLocalVdf(local, "112233441", "aabb"));
        CHECK(local.find("\"112233441\"\t\t\"aabb\"") != std::string::npos);
        std::string broken = "\"UserLocalConfigStore\"\n{\n}\n";
        CHECK(!steamtoken::PatchLocalVdf(broken, "k", "v"));
    }
    {
        std::string cfg = "\"InstallConfigStore\"\n{\n\t\"Accounts\"\n\t{\n"
                          "\t\t\"olduser\"\n\t\t{\n\t\t\t\"SteamID\"\t\t\"76561198000000001\"\n\t\t}\n\t}\n"
                          "\t\"AlwaysShowUserChooser\"\t\t\"1\"\n}\n";
        CHECK(steamtoken::PatchConfigVdf(cfg, "NewUser", "76561199086309324"));
        CHECK(cfg.find("\"AlwaysShowUserChooser\"\t\t\"0\"") != std::string::npos);
        CHECK(cfg.find("76561199086309324") != std::string::npos);
        CHECK(steamtoken::PatchConfigVdf(cfg, "OLDUSER", "76561198000000002"));
        CHECK(cfg.find("76561198000000002") != std::string::npos);
        CHECK(cfg.find("76561198000000001\"") == std::string::npos ||
              cfg.find("\"SteamID\"\t\t\"76561198000000001\"") == std::string::npos);
        std::string noacc = "\"InstallConfigStore\"\n{\n}\n";
        CHECK(!steamtoken::PatchConfigVdf(noacc, "x", "76561198000000001"));
    }
    {
        std::string lu = "\"users\"\n{\n\t\"76561198000000001\"\n\t{\n"
                         "\t\t\"AccountName\"\t\t\"first\"\n\t\t\"RememberPassword\"\t\t\"1\"\n"
                         "\t\t\"MostRecent\"\t\t\"1\"\n\t\t\"Timestamp\"\t\t\"1000\"\n\t}\n"
                         "\t\"76561198000000002\"\n\t{\n\t\t\"AccountName\"\t\t\"second\"\n"
                         "\t\t\"MostRecent\"\t\t\"0\"\n\t}\n}\n";
        CHECK(steamtoken::PatchLoginUsersVdf(lu, "Second", "76561198000000002", "2000"));
        size_t b1 = lu.find("\"76561198000000001\"");
        size_t b2 = lu.find("\"76561198000000002\"");
        CHECK(b1 != std::string::npos && b2 != std::string::npos && b1 < b2);
        std::string blk2 = lu.substr(b2, 600);
        CHECK(blk2.find("\"MostRecent\"\t\t\"1\"") != std::string::npos);
        CHECK(blk2.find("\"RememberPassword\"\t\t\"1\"") != std::string::npos);
        CHECK(blk2.find("\"AllowAutoLogin\"\t\t\"1\"") != std::string::npos);
        CHECK(blk2.find("\"Timestamp\"\t\t\"2000\"") != std::string::npos);
        std::string blk1 = lu.substr(b1, b2 - b1);
        CHECK(blk1.find("\"MostRecent\"\t\t\"1\"") == std::string::npos);
        CHECK(steamtoken::PatchLoginUsersVdf(lu, "third", "76561198000000003", "3000"));
        CHECK(lu.find("\"76561198000000003\"") != std::string::npos);
        CHECK(lu.find("\"AccountName\"\t\t\"third\"") != std::string::npos);
        std::string blk2b = lu.substr(lu.find("\"76561198000000002\""), 600);
        CHECK(blk2b.find("\"MostRecent\"\t\t\"0\"") != std::string::npos);
    }

    {
        std::string lu = "\"users\"\n{\n\t\"76561198000000001\"\n\t{\n"
                         "\t\t\"AccountName\"\t\t\"FirstUser\"\n\t\t\"SteamID\"\t\t\"76561198000000001\"\n"
                         "\t\t\"MostRecent\"\t\t\"1\"\n\t}\n"
                         "\t\"76561198000000002\"\n\t{\n\t\t\"AccountName\"\t\t\"second\"\n"
                         "\t\t\"MostRecent\"\t\t\"0\"\n\t}\n}\n";
        CHECK(steamtoken::ResolveAccountName(lu, "76561198000000001") == "FirstUser");
        CHECK(steamtoken::ResolveAccountName(lu, "76561198000000002") == "second");
        CHECK(steamtoken::ResolveAccountName(lu, "76561198000000003") == "");
        CHECK(steamtoken::ResolveAccountName("", "76561198000000001") == "");
        CHECK(steamtoken::ChildValue(lu, lu.find('{'),
                                     steamtoken::MatchBrace(lu, lu.find('{')),
                                     "Nope") == "");
    }

    if (g_fail == 0) printf("jwt: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
