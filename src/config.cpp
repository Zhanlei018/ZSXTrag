#include "config.h"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using json = nlohmann::json;

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

void override_environment(std::string& target, const char* name) {
    const std::string value = environment(name);
    if (!value.empty()) target = value;
}

void trim_trailing_slash(std::string& value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
}
}

std::filesystem::path Config::data_path() const {
    const std::filesystem::path path(data_dir);
    return path.is_absolute() ? path : root / path;
}

std::filesystem::path Config::web_path() const {
    const std::filesystem::path path(web_root);
    return path.is_absolute() ? path : root / path;
}

Config load_config(const std::filesystem::path& path) {
    Config config;
    config.root = std::filesystem::absolute(path).parent_path();

    if (std::filesystem::exists(path)) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("无法读取 config.json。");
        json data;
        try {
            input >> data;
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("config.json 格式错误：") + error.what());
        }
        if (!data.is_object()) throw std::runtime_error("config.json 必须是 JSON 对象。");

        config.kimi_api_key = data.value("kimi_api_key", config.kimi_api_key);
        config.kimi_base_url = data.value("kimi_base_url", config.kimi_base_url);
        config.kimi_model = data.value("kimi_model", config.kimi_model);
        config.wiki_base_url = data.value("wiki_base_url", config.wiki_base_url);
        config.wiki_rag_token = data.value("wiki_rag_token", config.wiki_rag_token);
        config.data_dir = data.value("data_dir", config.data_dir);
        config.web_root = data.value("web_root", config.web_root);
        config.port = data.value("port", config.port);
        config.top_k = data.value("top_k", config.top_k);
        config.chunk_size = data.value("chunk_size", config.chunk_size);
        config.chunk_overlap = data.value("chunk_overlap", config.chunk_overlap);
        config.max_context_chars = data.value("max_context_chars", config.max_context_chars);
    }

    override_environment(config.kimi_api_key, "MOONSHOT_API_KEY");
    override_environment(config.kimi_base_url, "KIMI_BASE_URL");
    override_environment(config.kimi_model, "KIMI_MODEL");
    override_environment(config.wiki_base_url, "WIKI_BASE_URL");
    override_environment(config.wiki_rag_token, "WIKI_RAG_TOKEN");

    trim_trailing_slash(config.kimi_base_url);
    trim_trailing_slash(config.wiki_base_url);
    if (config.kimi_base_url.empty() || config.kimi_model.empty()) {
        throw std::runtime_error("Kimi 接口地址和模型名不能为空。");
    }
    if (config.port < 1 || config.port > 65535) throw std::runtime_error("port 必须在 1 至 65535 之间。");
    if (config.top_k < 1 || config.top_k > 12) throw std::runtime_error("top_k 必须在 1 至 12 之间。");
    if (config.chunk_size < 200 || config.chunk_overlap >= config.chunk_size / 2) {
        throw std::runtime_error("chunk_size 至少为 200，chunk_overlap 必须小于 chunk_size 的一半。");
    }
    if (config.max_context_chars < 2000) throw std::runtime_error("max_context_chars 不能小于 2000。");
    return config;
}
