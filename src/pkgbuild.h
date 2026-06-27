#pragma once
#include <string>
#include <vector>
#include <utility>

enum class DownloadStatus { Ok, NotFound, NetworkError };

struct PkgbuildResult {
    std::string name;
    std::string content;
    DownloadStatus status;
};

std::vector<PkgbuildResult> fetch_pkgbuilds(const std::vector<std::string>& packages);
