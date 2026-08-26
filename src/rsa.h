#pragma once
#include <string>

std::string RsaEncryptPassword(const std::string& modHex, const std::string& expHex,
                               const std::string& password);
