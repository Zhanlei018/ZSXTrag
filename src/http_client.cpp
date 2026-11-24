#include "http_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
using Handle = struct Handle {
    HINTERNET value = nullptr;
    ~Handle() { if (value) WinHttpCloseHandle(value); }
};

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("文本不是有效的 UTF-8。");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), length);
    return result;
}

[[noreturn]] void winhttp_error(const std::string& action) {
    throw std::runtime_error(action + "失败（WinHTTP 错误 " + std::to_string(GetLastError()) + "）。");
}
}

std::string url_encode(const std::string& value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char byte : value) {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            output << static_cast<char>(byte);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
    }
    return output.str();
}

HttpResponse http_request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers) {
    const std::wstring wide_url = widen(url);
    std::wstring host(256, L'\0'), path(8192, L'\0'), extra(8192, L'\0');
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host.data();
    parts.dwHostNameLength = static_cast<DWORD>(host.size());
    parts.lpszUrlPath = path.data();
    parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    parts.lpszExtraInfo = extra.data();
    parts.dwExtraInfoLength = static_cast<DWORD>(extra.size());
    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts)) {
        winhttp_error("解析 URL");
    }
    host.resize(parts.dwHostNameLength);
    path.resize(parts.dwUrlPathLength);
    extra.resize(parts.dwExtraInfoLength);
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        throw std::runtime_error("仅支持 HTTP/HTTPS 接口地址。");
    }

    const bool local = host == L"127.0.0.1" || host == L"localhost" || host == L"::1";
    Handle session;
    session.value = WinHttpOpen(L"HXrag/1.0",
        local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) winhttp_error("初始化 HTTP 客户端");
    WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 180000);

    Handle connection;
    connection.value = WinHttpConnect(session.value, host.c_str(), parts.nPort, 0);
    if (!connection.value) winhttp_error("连接服务器");

    std::wstring object = path.empty() ? L"/" : path + extra;
    Handle request;
    request.value = WinHttpOpenRequest(connection.value, widen(method).c_str(), object.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request.value) winhttp_error("创建 HTTP 请求");

    std::wstring header_text;
    for (const auto& header : headers) {
        header_text += widen(header.first + ": " + header.second) + L"\r\n";
    }
    void* request_body = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD body_size = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.value,
            header_text.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_text.c_str(),
            header_text.empty() ? 0 : static_cast<DWORD>(-1L),
            request_body, body_size, body_size, 0)) {
        winhttp_error("发送 HTTP 请求");
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) winhttp_error("接收 HTTP 响应");

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        winhttp_error("读取 HTTP 状态");
    }

    HttpResponse response;
    response.status = static_cast<long>(status);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) winhttp_error("读取 HTTP 数据");
        if (available == 0) break;
        if (response.body.size() + available > 20 * 1024 * 1024) {
            throw std::runtime_error("HTTP 响应超过 20 MB。");
        }
        const std::size_t offset = response.body.size();
        response.body.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.value, response.body.data() + offset, available, &received)) {
            winhttp_error("接收 HTTP 数据");
        }
        response.body.resize(offset + received);
    }
    return response;
}
