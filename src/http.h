#pragma once
#include <string>

struct HttpResponse {
    bool ok = false;
    int status = 0;
    std::string body;
};

HttpResponse HttpPost(const std::string& host, const std::string& path,
                      const std::string& body, const std::string& proxy);
