#pragma once
#include "types.h"
#include <string>

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass,
                               const std::string& proxy);

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
}
