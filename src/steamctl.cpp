#include "steamctl.h"
#include "util.h"
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

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
