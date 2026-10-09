#include "steamctl.h"
#include "util.h"
#include "steamtoken.h"
#include <windows.h>
#include <dpapi.h>
#include <tlhelp32.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#pragma comment(lib, "crypt32.lib")

static std::string RegGetStr(HKEY root, const char* key, const char* value) {
    HKEY h = nullptr;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return "";
    char buf[1024] = {0};
    DWORD sz = sizeof(buf) - 1, type = 0;
    LSTATUS r = RegQueryValueExA(h, value, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || type != REG_SZ) return "";
    buf[sz] = 0;
    return std::string(buf);
}

std::string GetSteamPath() {
    std::string p = RegGetStr(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamExe");
    if (p.empty()) p = RegGetStr(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath");
    if (!p.empty() && p.find(".exe") == std::string::npos) {
        if (!p.empty() && p.back() != '\\') p += "\\";
        p += "steam.exe";
    }
    return p;
}

std::string GetSteamAutoLoginUser() {
    std::string u = RegGetStr(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "AutoLoginUser");
    if (!u.empty()) return u;
    std::string path = RegGetStr(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (path.empty()) return "";
    std::string vdf;
    if (!util::ReadTextFile(path + "/config/loginusers.vdf", vdf)) return "";
    std::string low = util::ToLower(vdf);
    size_t pos = 0;
    std::string best;
    while (true) {
        size_t blk = low.find("{", pos);
        if (blk == std::string::npos) break;
        size_t end = low.find("}", blk);
        if (end == std::string::npos) break;
        std::string section = low.substr(blk, end - blk);
        if (section.find("\"mostrecent\"\t\t\"1\"") != std::string::npos ||
            section.find("\"mostrecent\" \"1\"") != std::string::npos) {
            size_t an = low.find("\"accountname\"", blk);
            if (an < end) {
                size_t q1 = low.find('"', an + 13);
                size_t q2 = low.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    best = vdf.substr(q1 + 1, q2 - q1 - 1);
                    break;
                }
            }
        }
        pos = end + 1;
    }
    return best;
}

bool IsSteamRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "steam.exe") == 0) { found = true; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static DWORD FindSteamPid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "steam.exe") == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool KillSteam() {
    for (int attempt = 0; attempt < 3; attempt++) {
        DWORD pid = FindSteamPid();
        if (!pid) return true;
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (h) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 4000);
            CloseHandle(h);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    return FindSteamPid() == 0;
}

static bool ClearAutoLogin() {
    HKEY h = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS)
        return false;
    RegDeleteValueA(h, "AutoLoginUser");
    RegCloseKey(h);
    return true;
}

static bool LaunchSteam(const std::string& exe, const std::string& args) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exe + "\" " + args;
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != FALSE;
}

static bool KillByNames() {
    static const char* names[] = {"steam.exe", "steamservice.exe",
                                  "steamwebhelper.exe", "steamerrorreporter.exe",
                                  "streaming_client.exe"};
    for (int attempt = 0; attempt < 4; attempt++) {
        bool any = false;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                for (auto* n : names) {
                    if (_stricmp(pe.szExeFile, n) != 0) continue;
                    any = true;
                    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                           FALSE, pe.th32ProcessID);
                    if (h) {
                        TerminateProcess(h, 0);
                        WaitForSingleObject(h, 3000);
                        CloseHandle(h);
                    }
                    break;
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
        if (!any) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    return false;
}

static std::string ObfuscateToken(const std::string& name, const std::string& token) {
    DATA_BLOB bin{};
    bin.cbData = (DWORD)token.size();
    bin.pbData = (BYTE*)token.data();
    DATA_BLOB ent{};
    ent.cbData = (DWORD)name.size();
    ent.pbData = (BYTE*)name.data();
    DATA_BLOB bout{};
    if (!CryptProtectData(&bin, L"BObfuscateBuffer", &ent, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &bout))
        return "";
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bout.cbData * 2);
    for (DWORD i = 0; i < bout.cbData; i++) {
        out += hex[bout.pbData[i] >> 4];
        out += hex[bout.pbData[i] & 15];
    }
    LocalFree(bout.pbData);
    return out;
}

static bool RegSetLogin(const std::string& name) {
    HKEY h = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    RegSetValueExA(h, "AutoLoginUser", 0, REG_SZ,
                   (const BYTE*)name.c_str(), (DWORD)name.size() + 1);
    DWORD one = 1;
    RegSetValueExA(h, "RememberPassword", 0, REG_DWORD,
                   (const BYTE*)&one, sizeof(one));
    RegCloseKey(h);
    return true;
}

SteamLoginResult LoginByToken(const std::string& user, const std::string& token) {
    steamtoken::JwtClaims jc = steamtoken::ParseJwt(token);
    if (!jc.ok) return SteamLoginResult::BadToken;
    std::string exe = GetSteamPath();
    if (exe.empty()) return SteamLoginResult::NoSteam;
    size_t slash = exe.find_last_of("\\/");
    std::string root = slash == std::string::npos ? "." : exe.substr(0, slash);
    std::string cfgDir = root + "\\config";

    char localApp[MAX_PATH] = {0};
    DWORD lal = (DWORD)sizeof(localApp);
    std::string localVdf;
    if (GetEnvironmentVariableA("LOCALAPPDATA", localApp, lal) > 0 && lal < MAX_PATH)
        localVdf = std::string(localApp) + "\\Steam\\local.vdf";
    else
        localVdf = root + "\\config\\local.vdf";
    std::string configVdf = cfgDir + "\\config.vdf";
    std::string usersVdf = cfgDir + "\\loginusers.vdf";

    std::string local, config, users;
    if (!util::ReadTextFile(localVdf, local))
        local = "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"ConnectCache\"\n\t\t\t\t{\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
    if (!util::ReadTextFile(configVdf, config)) return SteamLoginResult::NoWrite;
    bool hasUsers = util::ReadTextFile(usersVdf, users);
    if (!hasUsers) users = "\"users\"\n{\n}\n";

    std::string given = user;
    for (auto& c : given)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    std::string name;
    if (!given.empty() && given != jc.sub) {
        name = given;
    } else if (hasUsers) {
        name = steamtoken::ResolveAccountName(users, jc.sub);
        for (auto& c : name)
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }
    if (name.empty()) name = given.empty() ? jc.sub : given;

    std::string blob = ObfuscateToken(name, token);
    if (blob.empty()) return SteamLoginResult::Failed;

    std::string key = steamtoken::CrcKey(name);
    long long ts = util::NowMs() / 1000;
    if (!steamtoken::PatchLocalVdf(local, key, blob)) return SteamLoginResult::NoWrite;
    if (!steamtoken::PatchConfigVdf(config, name, jc.sub)) return SteamLoginResult::NoWrite;
    if (!steamtoken::PatchLoginUsersVdf(users, name, jc.sub, std::to_string(ts)))
        return SteamLoginResult::NoWrite;

    std::string current = GetSteamAutoLoginUser();
    bool sameAccount = _stricmp(current.c_str(), name.c_str()) == 0;

    KillByNames();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!util::WriteTextFile(localVdf, local)) return SteamLoginResult::NoWrite;
    if (!util::WriteTextFile(configVdf, config)) return SteamLoginResult::NoWrite;
    if (!util::WriteTextFile(usersVdf, users)) return SteamLoginResult::NoWrite;
    RegSetLogin(name);

    if (!LaunchSteam(exe, "")) return SteamLoginResult::Failed;
    return sameAccount ? SteamLoginResult::Restarted : SteamLoginResult::Started;
}

SteamLoginResult LoginToAccount(const std::string& user, const std::string& pass) {
    std::string exe = GetSteamPath();
    if (exe.empty()) return SteamLoginResult::NoSteam;

    std::string current = GetSteamAutoLoginUser();
    bool sameAccount = _stricmp(current.c_str(), user.c_str()) == 0;

    if (sameAccount && IsSteamRunning())
        return SteamLoginResult::AlreadyActive;

    KillSteam();
    ClearAutoLogin();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string args = "-login \"" + user + "\" \"" + pass + "\"";
    if (!LaunchSteam(exe, args))
        return SteamLoginResult::Failed;

    return sameAccount ? SteamLoginResult::Restarted : SteamLoginResult::Started;
}
