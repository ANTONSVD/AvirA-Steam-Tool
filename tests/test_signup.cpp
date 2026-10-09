#include "../src/signup.h"
#include <cstdio>
#include <string>
#include <random>
#include <set>

static int g_fail = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #cond);               \
            g_fail++;                                                   \
        }                                                               \
    } while (0)

static bool HasClass(const std::string& s, const char* set) {
    for (char c : s)
        for (const char* p = set; *p; p++)
            if (c == *p) return true;
    return false;
}

int main() {
    std::mt19937 rng(12345);
    {
        std::set<std::string> seen;
        for (int i = 0; i < 300; i++) {
            std::string l = signup::GenLogin(rng);
            CHECK(l.size() >= 5 && l.size() <= 16);
            CHECK(l[0] >= 'a' && l[0] <= 'z');
            for (char c : l)
                CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
            seen.insert(l);
        }
        CHECK(seen.size() > 250);
    }
    {
        for (int i = 0; i < 100; i++) {
            std::string p = signup::GenPassword(rng);
            CHECK(p.size() == 14);
            CHECK(HasClass(p, "ABCDEFGHJKLMNPQRSTUVWXYZ"));
            CHECK(HasClass(p, "abcdefghijkmnpqrstuvwxyz"));
            CHECK(HasClass(p, "23456789"));
            CHECK(HasClass(p, "!@#$%^&*-_+="));
        }
    }
    {
        std::string m = signup::GenMailLogin(rng);
        CHECK(m.size() == 13);
        CHECK(m.compare(0, 5, "avira") == 0);
    }
    {
        std::string list =
            "{\"@context\":\"x\",\"hydra:member\":["
            "{\"@id\":\"/messages/aaa\",\"id\":\"aaa\",\"from\":{\"name\":\"Steam\",\"address\":\"noreply@steampowered.com\"},"
            "\"subject\":\"Verify your email\",\"seen\":false},"
            "{\"@id\":\"/messages/bbb\",\"id\":\"bbb\",\"from\":{\"name\":\"Spam\",\"address\":\"x@evil.com\"},"
            "\"subject\":\"Hello\",\"seen\":false}],\"hydra:totalItems\":2}";
        auto v = signup::ScanMessages(list);
        CHECK(v.size() == 2);
        if (v.size() == 2) {
            CHECK(v[0].id == "aaa");
            CHECK(v[0].from == "noreply@steampowered.com");
            CHECK(v[0].subject == "Verify your email");
            CHECK(v[1].id == "bbb");
        }
        CHECK(signup::ScanMessages("[]").empty());
        CHECK(signup::ScanMessages("").empty());
    }
    {
        CHECK(signup::LooksSteamMail("noreply@steampowered.com", "Verify"));
        CHECK(signup::LooksSteamMail("Steam Support", "hi"));
        CHECK(!signup::LooksSteamMail("x@evil.com", "Hello"));
    }
    {
        std::string msg = "{\"id\":\"aaa\",\"subject\":\"Verify\",\"text\":\"Hi! Confirm: "
                          "https://store.steampowered.com/join/verifymail?st=abc123&id=9. Done.\"}";
        std::string t = signup::MessageText(msg);
        CHECK(t.find("verifymail") != std::string::npos);
        std::string link = signup::ExtractVerifyLink(t);
        CHECK(link == "https://store.steampowered.com/join/verifymail?st=abc123&id=9");
    }
    {
        std::string msg = "{\"id\":\"aaa\",\"html\":[\"<p>Click <a href=\\\"https://store.steampowered.com/join/verifymail?a=1&amp;b=2\\\">here</a>.</p>\"]}";
        std::string t = signup::MessageText(msg);
        CHECK(t.find("Click") != std::string::npos);
        CHECK(t.find("https://") == std::string::npos);
        std::string link = signup::ExtractVerifyLink(msg);
        CHECK(link == "https://store.steampowered.com/join/verifymail?a=1&b=2");
    }
    {
        CHECK(signup::ExtractVerifyLink("no links here").empty());
        CHECK(signup::ExtractVerifyLink("https://store.steampowered.com/join/x").empty());
    }
    {
        CHECK(signup::HtmlUnescape("a&amp;b&lt;c&gt;") == "a&b<c>");
        CHECK(signup::StripTags("<p>Hi <b>there</b></p>").find("Hi") != std::string::npos);
    }
    {
        std::string pg = signup::CaptchaPageHtml("test-sitekey-123");
        CHECK(pg.find("test-sitekey-123") != std::string::npos);
        CHECK(pg.find("js.hcaptcha.com") != std::string::npos);
        CHECK(pg.find("hcaptcha.render") != std::string::npos);
        CHECK(pg.find("SITEKEY") == std::string::npos);
    }

    if (g_fail == 0) printf("signup: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
