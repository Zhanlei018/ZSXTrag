#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct Config {
    std::filesystem::path root;
    std::string kimi_api_key;
    std::string kimi_base_url = "https://api.moonshot.cn/v1";
    std::string kimi_model = "kimi-k3.1-pro";
    std::string wiki_base_url = "http://127.0.0.1:8787/api/wiki";
    std::string wiki_rag_token;
    std::string data_dir = "data";
    std::string web_root = "web";
    int port = 8790;
    std::size_t top_k = 5;
    std::size_t chunk_size = 700;
    std::size_t chunk_overlap = 100;
    std::size_t max_context_chars = 18000;

    std::filesystem::path data_path() const;
    std::filesystem::path web_path() const;
};

Config load_config(const std::filesystem::path& path);
