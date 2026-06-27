#include "pkgbuild.h"
#include <curl/curl.h>
#include <iostream>
#include <stdexcept>

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* buf) {
    size_t total = size * nmemb;
    buf->append(static_cast<char*>(data), total);
    return total;
}

static std::string curl_fetch(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl");

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "aursec/1.0");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) throw std::runtime_error(std::string("fetch failed: ") + curl_easy_strerror(res));
    return body;
}

static bool is_text_ext(const std::string& path) {
    std::string exts[] = {".sh", ".py", ".patch", ".install", ".pl", ".rb",
                          ".txt", ".cfg", ".conf", ".desktop", ".service",
                          ".timer", ".target", ".socket", ".path"};
    std::string ext;

    // Also accept files without extension but common names
    std::string names[] = {"Makefile", "makefile", "GNUmakefile",
                           "CMakeLists.txt", "configure", "meson.build",
                           "meson_options.txt"};
    for (const auto& n : names) {
        if (path == n || path.find("/" + n) != std::string::npos)
            return true;
    }

    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    ext = path.substr(dot);
    for (const auto& e : exts) {
        if (ext == e) return true;
    }
    return false;
}

static std::string extract_package_name(const std::string& pkgbuild) {
    size_t pos = pkgbuild.find("pkgname=");
    if (pos == std::string::npos) return {};
    pos += 8;
    std::string name;
    while (pos < pkgbuild.size() && pkgbuild[pos] != '\n') {
        if (pkgbuild[pos] != '\'' && pkgbuild[pos] != '"' && pkgbuild[pos] != ' ')
            name += pkgbuild[pos];
        pos++;
    }
    return name;
}

std::vector<PkgbuildResult> fetch_pkgbuilds(const std::vector<std::string>& packages) {
    std::vector<PkgbuildResult> results;
    for (const auto& name : packages) {
        std::string url = "https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=" + name;
        CURL* curl = curl_easy_init();
        if (!curl) { results.push_back({name, {}, DownloadStatus::NetworkError}); continue; }

        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "aursec/1.0");

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) results.push_back({name, {}, DownloadStatus::NetworkError});
        else if (http_code == 404) results.push_back({name, {}, DownloadStatus::NotFound});
        else results.push_back({name, body, DownloadStatus::Ok});
    }
    return results;
}

std::vector<std::pair<std::string, std::string>> fetch_aux_files(const std::string& name, const std::string& pkgbuild) {
    std::vector<std::pair<std::string, std::string>> files;
    std::string pkgname = extract_package_name(pkgbuild);
    if (pkgname.empty()) return files;

    // Try .install file
    {
        std::string url = "https://aur.archlinux.org/cgit/aur.git/plain/" + pkgname + ".install?h=" + name;
        std::cout << "  正在下载: " << pkgname << ".install" << std::endl;
        try {
            std::string content = curl_fetch(url);
            files.emplace_back(pkgname + ".install", content);
        } catch (...) {}
    }

    // Parse install= line from PKGBUILD
    size_t pos = pkgbuild.find("install=");
    while (pos != std::string::npos) {
        size_t end = pkgbuild.find('\n', pos);
        std::string line = pkgbuild.substr(pos, end - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string fname = line.substr(eq + 1);
            // strip quotes
            if (!fname.empty() && (fname[0] == '\'' || fname[0] == '"'))
                fname = fname.substr(1, fname.size() - 2);
            if (!fname.empty() && fname != pkgname + ".install") {
                std::string furl = "https://aur.archlinux.org/cgit/aur.git/plain/" + fname + "?h=" + name;
                std::cout << "  正在下载: " << fname << std::endl;
                try {
                    std::string content = curl_fetch(furl);
                    files.emplace_back(fname, content);
                } catch (...) {}
            }
        }
        pos = pkgbuild.find("install=", end);
    }
    return files;
}

std::vector<std::pair<std::string, std::string>> fetch_source_files(const std::string& name) {
    std::vector<std::pair<std::string, std::string>> files;

#ifndef HAVE_LIBARCHIVE
    (void)name;
    return files;
#else
    std::string tarball;
    // Download snapshot tarball
    {
        std::string url = "https://aur.archlinux.org/cgit/aur.git/snapshot/" + name + ".tar.gz";
        std::cout << "  正在下载源码快照: " << name << ".tar.gz" << std::endl;
        try {
            tarball = curl_fetch(url);
        } catch (...) { return files; }
    }

    if (tarball.empty()) return files;

    // Extract with libarchive
    struct archive* a = archive_read_new();
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);

    int r = archive_read_open_memory(a, tarball.data(), tarball.size());
    if (r != ARCHIVE_OK) { archive_read_free(a); return files; }

    struct archive_entry* entry;
    int count = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK && count < 10) {
        std::string pathname = archive_entry_pathname(entry);
        if (!is_text_ext(pathname)) continue;

        la_int64_t size = archive_entry_size(entry);
        if (size > 102400) continue; // skip files > 100KB

        std::string content(size, '\0');
        la_ssize_t read_bytes = archive_read_data(a, content.data(), content.size());
        if (read_bytes < 0) continue;
        content.resize(read_bytes);

        // Skip binary content
        bool binary = false;
        for (char c : content) {
            if (c == 0 && c != '\n' && c != '\r' && c != '\t') { binary = true; break; }
        }
        if (binary) continue;

        files.emplace_back(pathname, content);
        count++;
    }

    archive_read_free(a);
    return files;
#endif
}
