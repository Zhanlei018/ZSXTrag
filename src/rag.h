#pragma once

#include "config.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

class ApiError : public std::runtime_error {
public:
    ApiError(int status, const std::string& message);
    int status() const noexcept;

private:
    int status_;
};

class RagEngine {
public:
    explicit RagEngine(Config config);

    void reload(bool strict_wiki = true);
    nlohmann::json chat(const nlohmann::json& input);
    nlohmann::json retrieve(const std::string& query) const;
    nlohmann::json status() const;

private:
    struct Document {
        std::string id;
        std::string title;
        std::string content;
        std::string country;
        std::string topic;
        std::string page_id;
        int revision = 0;
        bool from_wiki = false;
        nlohmann::json citations = nlohmann::json::array();
    };

    struct Chunk {
        std::size_t document = 0;
        std::string text;
        int number = 0;
        std::unordered_map<std::string, int> terms;
        std::size_t length = 0;
    };

    struct Evidence {
        std::string id;
        nlohmann::json citation;
    };

    struct Retrieval {
        double score = 0;
        std::size_t chunk = 0;
    };

    Config config_;
    mutable std::mutex mutex_;
    std::vector<Document> documents_;
    std::vector<Chunk> chunks_;
    std::unordered_map<std::string, int> document_frequency_;
    double average_length_ = 1.0;
    int wiki_records_ = 0;
    std::string last_sync_;
    std::string last_error_;

    std::vector<Document> load_local_documents() const;
    std::vector<Document> load_wiki_documents() const;
    void build_index(std::vector<Document> documents);
    std::vector<Retrieval> search(const std::string& query) const;
    std::string ask_kimi(const nlohmann::json& messages) const;
};
