#pragma once
#include "config.h"
#include <string>
#include <vector>
#include <utility>

struct ReviewResult {
    bool passed = false;
    std::string reason;
};

std::vector<std::string> list_models(const Config& cfg);
ReviewResult review_pkgbuilds(const Config& cfg, const std::vector<std::pair<std::string, std::string>>& pkgs);
ReviewResult parse_review_response(const std::string& raw);
std::vector<std::string> ai_resolve_urls(const Config& cfg, const std::string& pkgbuild, const std::vector<std::string>& entries);
