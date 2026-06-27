#include "ai_reviewer.h"
#include "prompt.h"
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* buf) {
    size_t total = size * nmemb;
    buf->append(static_cast<char*>(data), total);
    return total;
}

static std::string api_post(const Config& cfg, const std::string& endpoint,
                            const std::string& body) {
    std::string url = cfg.base_url + endpoint;
    std::cout << "    正在调用 AI API: " << endpoint << std::endl;
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl");

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "aursec/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("API request failed: ") + curl_easy_strerror(res));
    if (http_code != 200)
        throw std::runtime_error("API returned HTTP " + std::to_string(http_code) + ": " + response);
    return response;
}

static std::string api_get(const Config& cfg, const std::string& endpoint) {
    std::string url = cfg.base_url + endpoint;
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl");

    std::string response;
    struct curl_slist* headers = nullptr;
    std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "aursec/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("API request failed: ") + curl_easy_strerror(res));
    if (http_code != 200)
        throw std::runtime_error("API returned HTTP " + std::to_string(http_code) + ": " + response);
    return response;
}

std::vector<std::string> list_models(const Config& cfg) {
    std::string resp = api_get(cfg, "/v1/models");
    auto j = nlohmann::json::parse(resp);
    std::vector<std::string> models;
    for (const auto& m : j["data"]) {
        models.push_back(m["id"].get<std::string>());
    }
    return models;
}

ReviewResult parse_review_response(const std::string& raw) {
    ReviewResult result;
    std::string trimmed = raw;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    auto starts_with = [](const std::string& s, const std::string& prefix) {
        if (s.size() < prefix.size()) return false;
        return std::equal(prefix.begin(), prefix.end(), s.begin(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    };

    if (starts_with(trimmed, "pass")) {
        result.passed = true;
        result.reason = trimmed.substr(4);
        size_t colon = result.reason.find(':');
        if (colon != std::string::npos) result.reason = result.reason.substr(colon + 1);
        result.reason.erase(0, result.reason.find_first_not_of(" \t\n\r"));
    } else if (starts_with(trimmed, "reject")) {
        result.passed = false;
        result.reason = trimmed.substr(6);
        size_t colon = result.reason.find(':');
        if (colon != std::string::npos) result.reason = result.reason.substr(colon + 1);
        result.reason.erase(0, result.reason.find_first_not_of(" \t\n\r"));
    } else {
        result.passed = false;
        result.reason = "unexpected response format: " + trimmed.substr(0, 100);
    }
    return result;
}

ReviewResult review_pkgbuilds(const Config& cfg,
                              const std::vector<std::pair<std::string, std::string>>& pkgs) {
    nlohmann::json body;
    body["model"] = cfg.model;
    body["temperature"] = 0.1;

    nlohmann::json msgs;
    msgs.push_back({{"role", "system"}, {"content", load_prompt(cfg)}});

    std::ostringstream user_content;
    user_content << "请审查以下 " << pkgs.size() << " 个 PKGBUILD：\n\n";
    for (const auto& [name, content] : pkgs) {
        user_content << "=== " << name << " ===\n" << content << "\n\n";
    }
    msgs.push_back({{"role", "user"}, {"content", user_content.str()}});

    body["messages"] = msgs;
    std::string req_body = body.dump();
    std::string resp = api_post(cfg, "/v1/chat/completions", req_body);
    auto j = nlohmann::json::parse(resp);
    std::string content = j["choices"][0]["message"]["content"].get<std::string>();
    return parse_review_response(content);
}
