#pragma once

#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
    long status = 0;
    std::string body;
};

HttpResponse http_request(
    const std::string& method,
    const std::string& url,
    const std::string& body = {},
    const std::vector<std::pair<std::string, std::string>>& headers = {});

std::string url_encode(const std::string& value);
