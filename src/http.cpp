#include "http.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(n ? n - 1 : 0);
    return w;
}

HttpResponse HttpPost(const std::string& host, const std::string& path,
                      const std::string& body, const std::string& proxy) {
    HttpResponse res;
    HINTERNET session = nullptr;
    if (proxy.empty()) {
        session = WinHttpOpen(L"AviraSteamTool/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            session = WinHttpOpen(L"AviraSteamTool/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    } else {
        std::wstring pw = Utf8ToWide(proxy);
        session = WinHttpOpen(L"AviraSteamTool/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                              pw.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) return res;

    WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);

    std::wstring whost = Utf8ToWide(host);
    std::wstring wpath = Utf8ToWide(path);

    HINTERNET connect = WinHttpConnect(session, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = nullptr;
    if (connect)
        request = WinHttpOpenRequest(connect, L"POST", wpath.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
    BOOL sent = FALSE;
    if (request) {
        std::wstring headers =
            L"Content-Type: application/x-www-form-urlencoded\r\n"
            L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n";
        sent = WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L,
                                  (LPVOID)body.data(), (DWORD)body.size(),
                                  (DWORD)body.size(), 0);
        if (sent && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            res.status = (int)status;
            std::string chunk;
            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail)) break;
                if (!avail) break;
                chunk.resize(avail);
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
                chunk.resize(read);
                res.body += chunk;
            } while (avail > 0);
            res.ok = status >= 200 && status < 300;
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}
