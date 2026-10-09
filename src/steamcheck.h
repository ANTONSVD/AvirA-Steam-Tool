#pragma once
#include "types.h"
#include "signup.h"
#include <string>
#include <vector>

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass,
                                const std::string& proxy);
CheckOutcome CheckSteamToken(const std::string& token, const std::string& proxy);

struct SignupBox {
    std::string login;
    std::string domain;
    std::string email;
    std::string password;
    std::string token;
};

bool TempMailCreate(SignupBox& box);
std::vector<signup::MailMsg> TempMailMessages(const SignupBox& box);
std::string TempMailRead(const SignupBox& box, const std::string& id);
bool SteamVerifyGet(const std::string& url);

struct BanResult {
    bool ok = false;
    std::string ban;
    int days = 0;
};

BanResult FetchBanBySteamId(const std::string& steamid);
BanResult FetchBanByAccountName(const std::string& name);

namespace steamcheck {
int CurrentMinInterval();
void ReportMinInterval(int ms);
void SetBanCheck(bool on);
void SetLogPath(const std::string& path);
}
