#pragma once
#include <string>
#include <vector>
#include <utility>

struct PkgbuildResult {
    std::string name;
    std::string content;
    bool success;
};

std::vector<PkgbuildResult> fetch_pkgbuilds(const std::vector<std::string>& packages);
