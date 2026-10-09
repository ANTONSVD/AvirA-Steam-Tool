#include "../src/accounts.h"
#include <cstdio>
#include <string>
#include <windows.h>

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

static std::string MakeJwt(const std::string& sub) {
    std::string p = "{\"iss\":\"steam\",\"sub\":\"" + sub +
                    "\",\"aud\":[\"client\"],\"exp\":4102444800}";
    return B64("{\"typ\":\"JWT\",\"alg\":\"EdDSA\"}") + "." + B64(p) + "." + B64("sig");
}

int main() {
    char tmp[MAX_PATH] = {0};
    GetEnvironmentVariableA("TEMP", tmp, MAX_PATH);
    std::string acc = std::string(tmp) + "\\tacc.txt";
    std::string hits = std::string(tmp) + "\\thits.txt";

    std::string tok = MakeJwt("76561198000000001");
    {
        auto v = ParseCombos("gamer1:" + tok);
        CHECK(v.size() == 1);
        if (!v.empty()) {
            CHECK(v[0].user == "gamer1");
            CHECK(v[0].token == tok);
            CHECK(v[0].pass.empty());
        }
    }
    {
        auto v = ParseCombos("gamer1----" + tok);
        CHECK(v.size() == 1);
        if (!v.empty()) {
            CHECK(v[0].user == "gamer1");
            CHECK(v[0].token == tok);
        }
    }
    {
        auto v = ParseCombos(tok);
        CHECK(v.size() == 1);
        if (!v.empty()) {
            CHECK(v[0].user == "76561198000000001");
            CHECK(v[0].token == tok);
        }
    }
    {
        auto v = ParseCombos("gamer1:plainpass");
        CHECK(v.size() == 1);
        if (!v.empty()) {
            CHECK(v[0].token.empty());
            CHECK(v[0].pass == "plainpass");
        }
    }
    {
        auto v = ParseCombos("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0In0.aaaa");
        CHECK(v.empty());
    }    {
        AccountStore st;
        Cred c;
        c.user = "gamer1";
        c.pass = "pw";
        CHECK(st.AddOrUpdate(c, AccStatus::Valid));
        st.SetToken("gamer1", tok);
        st.SetSteamId("gamer1", "76561198000000001");
        st.Save(acc);
        AccountStore st2;
        st2.Load(acc);
        CHECK(st2.Items().size() == 1);
        if (!st2.Items().empty()) {
            CHECK(st2.Items()[0].token == tok);
            CHECK(st2.Items()[0].steamid == "76561198000000001");
            CHECK(st2.Items()[0].pass == "pw");
        }
        ExportHits(hits, st2.Items());
        std::string hd;
        FILE* f = fopen(hits.c_str(), "rb");
        CHECK(f != nullptr);
        if (f) {
            char buf[8192] = {0};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            hd.assign(buf, n);
        }
        CHECK(hd == "gamer1:" + tok + "\n");
    }

    {
        std::string mixed = "user1:pass1\n"
                            "gamer1:" + tok + "\n"
                            "gamer2----" + tok + "\n" +
                            tok + "\n"
                            "# comment\n"
                            "user2:p2:mail:mpp\n"
                            "\n";
        std::string stripped = StripTokenLines(mixed);
        CHECK(stripped.find(tok) == std::string::npos);
        CHECK(stripped.find("user1:pass1") != std::string::npos);
        CHECK(stripped.find("user2:p2:mail:mpp") != std::string::npos);
        CHECK(stripped.find("# comment") != std::string::npos);
        CHECK(ParseCombos(stripped).size() == 2);
        CHECK(StripTokenLines("user1:pass1\n").find("user1:pass1") != std::string::npos);
        CHECK(StripTokenLines("").empty());
    }

    if (g_fail == 0) printf("accounts: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
