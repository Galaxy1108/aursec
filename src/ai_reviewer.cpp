#include "ai_reviewer.h"
#include "prompt.h"
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

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
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

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
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

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
        user_content << "=== " << name << " ===\n";
        std::istringstream stream(content);
        std::string line;
        int lineno = 1;
        while (std::getline(stream, line))
            user_content << lineno++ << "│" << line << "\n";
        user_content << "\n";
    }
    msgs.push_back({{"role", "user"}, {"content", user_content.str()}});

    body["messages"] = msgs;
    std::string req_body = body.dump();
    std::string resp = api_post(cfg, "/v1/chat/completions", req_body);
    auto j = nlohmann::json::parse(resp);
    std::string content = j["choices"][0]["message"]["content"].get<std::string>();
    return parse_review_response(content);
}

ExpandResult ai_expand_and_find_urls(const Config& cfg,
    const std::vector<std::pair<std::string, std::string>>& files)
{
    ExpandResult result;

    nlohmann::json body;
    body["model"] = cfg.model;
    body["temperature"] = 0.1;

    nlohmann::json msgs;
    msgs.push_back({{"role", "system"}, {"content",
        "你是一个 PKGBUILD 分析助手。分析提供的文件，完成以下任务：\n"
        "1. 展开所有文件中的 bash 变量（如 ${pkgver}、${_pkgname}、${pkgname%-bin} 等）\n"
        "2. 识别 source=()、build()、package() 以及 .install 脚本中所有 curl/wget 的 URL\n"
        "返回格式：\n"
        "---EXPANDED---\n"
        "[展开变量后的所有文件，保持文件名和 === 标记不变]\n"
        "---URLS---\n"
        "[每行一个 http/https 链接]"}});

    std::ostringstream user;
    for (const auto& [name, content] : files) {
        user << "=== " << name << " ===\n" << content << "\n\n";
    }
    msgs.push_back({{"role", "user"}, {"content", user.str()}});

    body["messages"] = msgs;
    std::string resp = api_post(cfg, "/v1/chat/completions", body.dump());
    auto j = nlohmann::json::parse(resp);
    std::string full = j["choices"][0]["message"]["content"].get<std::string>();

    // Parse expanded section
    size_t expanded_end = full.find("---URLS---");
    std::string expanded_section = (expanded_end != std::string::npos) ? full.substr(0, expanded_end) : full;

    // Parse expanded files (=== name === ...)
    std::regex file_re(R"(=== (.+?) ===\s*\n(.*?)(?=\n=== |\Z))");
    std::sregex_iterator fit(expanded_section.begin(), expanded_section.end(), file_re);
    for (; fit != std::sregex_iterator(); ++fit) {
        std::string fname = (*fit)[1].str();
        std::string fcontent = (*fit)[2].str();
        // Remove trailing whitespace
        while (!fcontent.empty() && (fcontent.back() == '\n' || fcontent.back() == '\r')) fcontent.pop_back();
        if (!fname.empty())
            result.expanded_files.emplace_back(fname, fcontent);
    }
    // Fallback: if regex didn't match, use expanded section as-is
    if (result.expanded_files.empty() && !expanded_section.empty())
        result.expanded_files = files;

    // Parse URLs section
    std::string url_section = (expanded_end != std::string::npos) ? full.substr(expanded_end + 9) : "";
    std::istringstream us(url_section);
    std::string line;
    while (std::getline(us, line)) {
        if (line.find("http://") == 0 || line.find("https://") == 0)
            result.urls.push_back(line);
    }

    return result;
}
