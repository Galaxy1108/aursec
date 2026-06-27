#pragma once
#include "config.h"
#include <string>
#include <vector>
#include <utility>

struct ReviewResult {
    bool passed = false;
    std::string reason;
};

struct ExpandResult {
    std::vector<std::pair<std::string, std::string>> expanded_files;
    std::vector<std::string> urls;
};

std::vector<std::string> list_models(const Config& cfg);
ReviewResult review_pkgbuilds(const Config& cfg, const std::vector<std::pair<std::string, std::string>>& pkgs);
ReviewResult parse_review_response(const std::string& raw);
ExpandResult ai_expand_and_find_urls(const Config& cfg, const std::vector<std::pair<std::string, std::string>>& files);
