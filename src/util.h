#pragma once
#include <string>
#include <vector>

namespace util {

std::string Trim(const std::string& s);
std::string ToLower(const std::string& s);
std::string UrlEncode(const std::string& s);
std::string Base64Encode(const unsigned char* data, size_t len);
std::vector<std::string> SplitLines(const std::string& s);
bool ReadTextFile(const std::string& path, std::string& out);
bool WriteTextFile(const std::string& path, const std::string& out);
bool AppendTextFile(const std::string& path, const std::string& line);
std::string ExeDir();
std::string FormatClock(long long unixTime);
long long NowMs();

}
