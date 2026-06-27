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
std::vector<std::pair<std::string, std::string>> fetch_aux_files(const std::string& name, const std::string& pkgbuild);
std::vector<std::pair<std::string, std::string>> fetch_source_files(const std::string& name);
