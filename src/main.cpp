#include "config.h"
#include "rag.h"
#include "server.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {
std::vector<std::string> windows_arguments() {
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) throw std::runtime_error("无法读取命令行参数。");
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, values[i], -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) {
            result.emplace_back();
            continue;
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, values[i], -1, value.data(), length, nullptr, nullptr);
        value.resize(static_cast<std::size_t>(length - 1));
        result.push_back(std::move(value));
    }
    LocalFree(values);
    return result;
}

void usage() {
    std::cout <<
        "HXrag - C++ 卫星频规 RAG\n\n"
        "用法（在 HXrag 目录执行）：\n"
        "  build\\hxrag.exe serve                 启动网页与 HTTP API\n"
        "  build\\hxrag.exe retrieve \"问题\"       只检索，不调用模型\n"
        "  build\\hxrag.exe ask \"问题\"            检索并调用 Kimi 回答\n"
        "  build\\hxrag.exe sync                  重新载入本地资料并同步 Wiki\n"
        "  build\\hxrag.exe --config 其他配置.json serve\n\n"
        "API Key 填在 config.json 的 kimi_api_key，也可使用 MOONSHOT_API_KEY 环境变量。\n";
}
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    try {
        const std::vector<std::string> command_line = windows_arguments();
        std::filesystem::path config_path = std::filesystem::current_path() / "config.json";
        std::vector<std::string> arguments;
        for (std::size_t i = 1; i < command_line.size(); ++i) {
            const std::string& argument = command_line[i];
            if (argument == "--config") {
                if (++i >= command_line.size()) throw std::runtime_error("--config 后需要配置文件路径。");
                config_path = std::filesystem::absolute(command_line[i]);
            } else {
                arguments.push_back(argument);
            }
        }
        const std::string command = arguments.empty() ? "serve" : arguments[0];
        if (command == "help" || command == "--help" || command == "-h") {
            usage();
            return 0;
        }
        const Config config = load_config(config_path);
        RagEngine engine(config);
        if (command == "sync") {
            engine.reload(true);
            std::cout << engine.status().dump(2) << '\n';
            return 0;
        }

        engine.reload(false);
        const auto status = engine.status();
        std::cout << "已索引 " << status["documents"].get<int>() << " 份资料，"
                  << status["chunks"].get<int>() << " 个文本块。\n";
        if (!status["lastError"].get<std::string>().empty()) {
            std::cerr << "Wiki 同步未完成：" << status["lastError"].get<std::string>() << '\n';
        }
        if (command == "serve") {
            if (!status["kimiKeyConfigured"].get<bool>()) {
                std::cout << "提示：尚未配置 Kimi API Key；检索可用，问答前请编辑 config.json。\n";
            }
            return run_server(engine, config);
        }
        if ((command == "retrieve" || command == "ask") && arguments.size() >= 2) {
            const std::string question = arguments[1];
            if (command == "retrieve") {
                std::cout << engine.retrieve(question).dump(2) << '\n';
            } else {
                const nlohmann::json input = {{"messages", nlohmann::json::array({
                    {{"role", "user"}, {"content", question}}
                })}};
                std::cout << engine.chat(input).dump(2) << '\n';
            }
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "错误：" << error.what() << '\n';
        return 1;
    }
}
