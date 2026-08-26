#pragma once
#include "types.h"
#include <string>

CheckOutcome CheckSteamAccount(const std::string& user, const std::string& pass,
                               const std::string& proxy);
