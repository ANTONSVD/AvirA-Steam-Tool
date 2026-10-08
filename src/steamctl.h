#pragma once
#include <string>

std::string GetSteamPath();
std::string GetSteamAutoLoginUser();
bool IsSteamRunning();
bool KillSteam();

enum class SteamLoginResult {
    Started,
    Restarted,
    AlreadyActive,
    NoSteam,
    Failed,
    BadToken,
    NoWrite
};

SteamLoginResult LoginToAccount(const std::string& user, const std::string& pass);
SteamLoginResult LoginByToken(const std::string& user, const std::string& token);
