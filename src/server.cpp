#include "server.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <csignal>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace {
using json = nlohmann::json;
constexpr std::size_t max_header_bytes = 32 * 1024;
constexpr std::size_t max_body_bytes = 2 * 1024 * 1024;
SOCKET listening_socket = INVALID_SOCKET;

struct Request {
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

bool send_all(SOCKET socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

std::string status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

bool loopback_origin(const std::string& origin) {
    for (const std::string& prefix : {std::string("http://127.0.0.1:"), std::string("http://localhost:")}) {
        if (origin.rfind(prefix, 0) != 0) continue;
        const std::string port = origin.substr(prefix.size());
        return !port.empty() && std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); });
    }
    return false;
}

void respond(SOCKET socket, int status, const std::string& body,
             const std::string& content_type = "application/json; charset=utf-8",
             const std::string& origin = "") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Cache-Control: no-store\r\n"
             << "X-Content-Type-Options: nosniff\r\n";
    if (loopback_origin(origin)) {
        response << "Access-Control-Allow-Origin: " << origin << "\r\n"
                 << "Vary: Origin\r\n";
    }
    response << "\r\n" << body;
    send_all(socket, response.str());
}

void respond_json(SOCKET socket, int status, const json& data, const std::string& origin = "") {
    respond(socket, status, data.dump(), "application/json; charset=utf-8", origin);
}

Request receive_request(SOCKET socket) {
    std::string data;
    std::size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() >= max_header_bytes) throw ApiError(413, "请求头过大。");
        char buffer[4096];
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) throw ApiError(400, "请求不完整。");
        data.append(buffer, static_cast<std::size_t>(received));
    }
    header_end += 4;
    std::istringstream input(data.substr(0, header_end));
    Request request;
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line);
    std::string version;
    first >> request.method >> request.target >> version;
    if (request.method.empty() || request.target.empty() || version.rfind("HTTP/", 0) != 0) {
        throw ApiError(400, "HTTP 请求格式不正确。");
    }
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        request.headers[lower(line.substr(0, colon))] = trim(line.substr(colon + 1));
    }
    std::size_t content_length = 0;
    if (request.headers.count("content-length")) {
        try {
            content_length = std::stoull(request.headers["content-length"]);
        } catch (...) {
            throw ApiError(400, "Content-Length 不正确。");
        }
    }
    if (content_length > max_body_bytes) throw ApiError(413, "请求正文超过 2 MB。");
    request.body = data.substr(header_end);
    while (request.body.size() < content_length) {
        char buffer[8192];
        const int wanted = static_cast<int>(std::min(sizeof(buffer), content_length - request.body.size()));
        const int received = recv(socket, buffer, wanted, 0);
        if (received <= 0) throw ApiError(400, "请求正文不完整。");
        request.body.append(buffer, static_cast<std::size_t>(received));
    }
    if (request.body.size() > content_length) request.body.resize(content_length);
    return request;
}

json request_json(const Request& request) {
    try {
        return json::parse(request.body.empty() ? "{}" : request.body);
    } catch (...) {
        throw ApiError(400, "请求必须是有效的 JSON。");
    }
}

void handle_client(SOCKET socket, RagEngine& engine, const Config& config) {
    struct Guard {
        SOCKET value;
        ~Guard() { if (value != INVALID_SOCKET) closesocket(value); }
    } guard{socket};
    try {
        const Request request = receive_request(socket);
        const auto origin_it = request.headers.find("origin");
        const std::string origin = origin_it == request.headers.end() ? "" : origin_it->second;
        if (request.headers.count("host")) {
            const std::string expected = ":" + std::to_string(config.port);
            const std::string host = lower(request.headers.at("host"));
            if (host != "127.0.0.1" + expected && host != "localhost" + expected) {
                respond_json(socket, 403, {{"error", "请求地址无效。"}}, origin);
                return;
            }
        }
        const std::string path = request.target.substr(0, request.target.find('?'));
        if (request.method == "OPTIONS" && path.rfind("/api/", 0) == 0) {
            std::ostringstream response;
            response << "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n"
                     << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     << "Access-Control-Allow-Headers: Content-Type\r\n"
                     << "Access-Control-Max-Age: 600\r\n";
            if (loopback_origin(origin)) response << "Access-Control-Allow-Origin: " << origin << "\r\nVary: Origin\r\n";
            response << "\r\n";
            send_all(socket, response.str());
            return;
        }
        if (path == "/healthz" && request.method == "GET") {
            respond_json(socket, 200, {{"ok", true}}, origin);
            return;
        }
        if (path == "/api/rag/status" && request.method == "GET") {
            respond_json(socket, 200, engine.status(), origin);
            return;
        }
        if (path == "/api/rag/sync" && request.method == "POST") {
            engine.reload(true);
            respond_json(socket, 200, engine.status(), origin);
            return;
        }
        if (path == "/api/rag/retrieve" && request.method == "POST") {
            const json input = request_json(request);
            if (!input.contains("query") || !input["query"].is_string() || trim(input["query"].get<std::string>()).empty()) {
                throw ApiError(400, "请输入检索内容。");
            }
            respond_json(socket, 200, engine.retrieve(input["query"].get<std::string>()), origin);
            return;
        }
        if (path == "/api/rag/chat" && request.method == "POST") {
            respond_json(socket, 200, engine.chat(request_json(request)), origin);
            return;
        }
        if ((path == "/" || path == "/index.html") && request.method == "GET") {
            std::ifstream file(config.web_path() / "index.html", std::ios::binary);
            if (!file) throw std::runtime_error("找不到 web/index.html。");
            const std::string html((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            respond(socket, 200, html, "text/html; charset=utf-8", origin);
            return;
        }
        respond_json(socket, 404, {{"error", "接口不存在。"}}, origin);
    } catch (const ApiError& error) {
        respond_json(socket, error.status(), {{"error", error.what()}});
    } catch (const json::exception&) {
        respond_json(socket, 400, {{"error", "请求 JSON 格式不正确。"}});
    } catch (const std::exception& error) {
        respond_json(socket, 500, {{"error", error.what()}});
    }
}

void stop_server(int) {
    if (listening_socket != INVALID_SOCKET) closesocket(listening_socket);
    listening_socket = INVALID_SOCKET;
}
}

int run_server(RagEngine& engine, const Config& config) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "无法初始化 Windows 网络组件。\n";
        return 1;
    }
    listening_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listening_socket == INVALID_SOCKET) {
        WSACleanup();
        std::cerr << "无法创建监听 socket。\n";
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(config.port));
    if (bind(listening_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listening_socket, 16) == SOCKET_ERROR) {
        std::cerr << "无法监听 127.0.0.1:" << config.port << "，端口可能被占用。\n";
        stop_server(0);
        WSACleanup();
        return 1;
    }
    std::signal(SIGINT, stop_server);
    std::signal(SIGTERM, stop_server);
    std::cout << "HXrag 已启动：http://127.0.0.1:" << config.port << "\n"
              << "API：POST /api/rag/chat；按 Ctrl+C 停止。\n" << std::flush;
    for (;;) {
        SOCKET client = accept(listening_socket, nullptr, nullptr);
        if (client == INVALID_SOCKET) break;
        DWORD timeout = 20000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::thread(handle_client, client, std::ref(engine), std::cref(config)).detach();
    }
    if (listening_socket != INVALID_SOCKET) closesocket(listening_socket);
    listening_socket = INVALID_SOCKET;
    WSACleanup();
    std::cout << "HXrag 已停止。\n";
    return 0;
}
