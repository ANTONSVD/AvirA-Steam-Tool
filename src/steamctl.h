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
    Failed
};

SteamLoginResult LoginToAccount(const std::string& user, const std::string& pass);
