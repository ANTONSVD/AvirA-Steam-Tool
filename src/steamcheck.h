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

struct JoinCtx {
    std::string cookies;
    int count = 0;
};

struct JoinCaptchaInfo {
    std::string gid;
    int type = 0;
    std::string sitekey;
};

bool JoinBegin(JoinCtx& ctx);
bool JoinCaptcha(JoinCtx& ctx, JoinCaptchaInfo& out);
bool JoinVerifyEmail(JoinCtx& ctx, const std::string& email,
                     const std::string& gid, const std::string& captchaToken,
                     std::string& creationId, int& code, std::string& details);
int JoinPollVerified(JoinCtx& ctx, const std::string& creationId);
bool JoinCheckAvail(JoinCtx& ctx, const std::string& name,
                    const std::string& creationId);
struct JoinResult {
    bool ok = false;
    std::string msg;
};
JoinResult JoinCreate(JoinCtx& ctx, const std::string& name,
                     const std::string& pass, const std::string& creationId);

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
