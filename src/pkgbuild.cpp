#include "pkgbuild.h"
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
#include <cstring>

#define RST   "\033[0m"
#define RED   "\033[31m"
#define GREEN "\033[32m"
#define YELL  "\033[33m"

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* buf) {
    size_t total = size * nmemb;
    buf->append(static_cast<char*>(data), total);
    return total;
}

static std::string popen_fetch(const std::string& url, int timeout_sec = 15, bool fail_on_http_error = true) {
    std::string cmd = "curl -s -L --connect-timeout " + std::to_string(timeout_sec / 2)
        + " --max-time " + std::to_string(timeout_sec)
        + (fail_on_http_error ? " -f" : "") + " -o- " + url;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");

    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;

    int status = pclose(pipe);
    if (status != 0 || result.empty())
        throw std::runtime_error("curl exit code " + std::to_string(status));
    return result;
}

static std::string curl_fetch(const std::string& url) {
    return popen_fetch(url);
}

static void popen_fetch_progress(const std::string& url, std::string& data, double& size_mb) {
    // HEAD: get size from Content-Length
    curl_off_t total = 0;
    {
        std::string hcmd = "curl -sI -L --connect-timeout 5 --max-time 10 -o /dev/null -w '%{content_length_download}' " + url + " 2>/dev/null";
        FILE* hpipe = popen(hcmd.c_str(), "r");
        char hbuf[64];
        if (hpipe) {
            if (fgets(hbuf, sizeof(hbuf), hpipe)) {
                try { total = std::stoll(hbuf); } catch (...) { total = 0; }
            }
            pclose(hpipe);
        }
        // If no Content-Length, try range request to probe
        if (total <= 0) {
            std::string rcmd = "curl -sL -r 0-0 --connect-timeout 5 --max-time 10 -o /dev/null -w '%{content_length_download}' " + url + " 2>/dev/null";
            FILE* rpipe = popen(rcmd.c_str(), "r");
            char rbuf[64];
            if (rpipe) {
                if (fgets(rbuf, sizeof(rbuf), rpipe)) {
                    try {
                        curl_off_t range_size = std::stoll(rbuf);
                        if (range_size > 0) total = range_size;
                    } catch (...) {}
                }
                pclose(rpipe);
            }
        }
        if (total > 50 * 1024 * 1024) {
            throw std::runtime_error("file too large (>50MB)");
        }
    }

    // Download with progress
    std::string cmd = "curl -s -L -f --connect-timeout 15 --max-time 120 -o- " + url;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");

    char buf[8192];
    size_t received = 0;
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), pipe);
        if (n == 0) break;
        data.append(buf, n);
        received += n;

        if (received > 50ULL * 1024 * 1024) {
            pclose(pipe);
            data.clear();
            throw std::runtime_error("file too large (>50MB)");
        }

        if (total > 0) {
            int pct = static_cast<int>(received * 100 / total);
            int bar_w = 20;
            int fill = pct * bar_w / 100;
            std::cerr << "\r\033[K     [" << std::string(fill, '#')
                      << std::string(bar_w - fill, ' ') << "] " << pct << "%" << std::flush;
        } else {
            std::cerr << "\r\033[K     " << (received / (1024 * 1024)) << " MB" << std::flush;
        }
    }

    int status = pclose(pipe);
    size_mb = (double)received / (1024.0 * 1024.0);

    if (status != 0 || data.empty())
        throw std::runtime_error("curl exit code " + std::to_string(status));
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

static std::map<std::string, std::string> extract_all_vars(const std::string& pkgbuild) {
    std::map<std::string, std::string> vars;
    std::istringstream stream(pkgbuild);
    std::string line;
    while (std::getline(stream, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        if (k.empty() || k.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_") != std::string::npos)
            continue;
        std::string v = line.substr(eq + 1);
        if (!v.empty() && (v[0] == '\'' || v[0] == '"')) v = v.substr(1);
        if (!v.empty() && (v.back() == '\'' || v.back() == '"')) v.pop_back();
        if (!v.empty() && (v.back() == '\'' || v.back() == '"')) v.pop_back();
        vars[k] = v;
    }
    return vars;
}

static std::string resolve_vars(const std::string& raw, const std::map<std::string, std::string>& vars) {
    std::string r = raw;
    for (const auto& [k, v] : vars) {
        if (v.empty()) continue;
        size_t p = 0;
        std::string braced = "${" + k + "}";
        while ((p = r.find(braced, p)) != std::string::npos) { r.replace(p, braced.size(), v); }
        p = 0;
        std::string unbraced = "$" + k;
        while ((p = r.find(unbraced, p)) != std::string::npos) { r.replace(p, unbraced.size(), v); }
    }
    return r;
}

static std::string resolve_install_name(const std::string& raw, const std::map<std::string, std::string>& vars) {
    return resolve_vars(raw, vars);
}

static std::string extract_var(const std::string& pkgbuild, const std::string& var) {
    std::string pat = var + "=";
    size_t pos = pkgbuild.find(pat);
    if (pos == std::string::npos) return {};
    pos += pat.size();
    std::string val;
    while (pos < pkgbuild.size() && pkgbuild[pos] != '\n') {
        if (pkgbuild[pos] != '\'' && pkgbuild[pos] != '"' && pkgbuild[pos] != ' ')
            val += pkgbuild[pos];
        pos++;
    }
    return val;
}

static std::string fmt_size(double bytes) {
    if (bytes < 1024) return std::to_string((int)bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string((int)(bytes / 1024)) + " KB";
    return std::to_string((int)(bytes / (1024 * 1024))) + " MB";
}

static bool is_archive_ext(const std::string& path) {
    std::string exts[] = {".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst",
                          ".tar", ".tgz", ".tbz2", ".txz", ".zip"};
    for (const auto& e : exts) {
        if (path.size() >= e.size() && path.substr(path.size() - e.size()) == e)
            return true;
    }
    return false;
}

// REMOVED: popen_fetch_with_size replaced by popen_fetch_progress

#ifdef HAVE_LIBARCHIVE
static void extract_from_memory(const std::string& data, std::vector<std::pair<std::string, std::string>>& files,
                                int& count, int max_files, const std::string& label) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    int r = archive_read_open_memory(a, data.data(), data.size());
    if (r != ARCHIVE_OK) {
        std::cerr << RED "    解包失败: 无法打开归档" RST << std::endl;
        archive_read_free(a);
        return;
    }

    struct archive_entry* entry;
    int extracted = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string pathname = archive_entry_pathname(entry);
        la_int64_t size = archive_entry_size(entry);
        std::string size_str = fmt_size((double)size);

        if (is_text_ext(pathname) && size <= 102400 && count < max_files) {
            std::string content(static_cast<size_t>(size), '\0');
            la_ssize_t read_bytes = archive_read_data(a, content.data(), content.size());
            if (read_bytes < 0) continue;
            content.resize(read_bytes);

            bool binary = false;
            for (char c : content) {
                if (c == 0 && c != '\n' && c != '\r' && c != '\t') { binary = true; break; }
            }
            if (binary) {
                std::cout << "    文件: " << pathname << " (" << size_str << ") " YELL "跳过（二进制）" RST << std::endl;
                continue;
            }
            std::cout << "    文件: " << pathname << " (" << size_str << ") " GREEN "通过" RST << std::endl;
            files.emplace_back(label + "/" + pathname, content);
            count++;
            extracted++;
        } else {
            std::string reason;
            if (size > 102400) reason = "过大";
            else if (!is_text_ext(pathname)) reason = "非文本";
            else reason = "已达上限";
            std::cout << "    文件: " << pathname << " (" << size_str << ") " YELL "跳过（" << reason << "）" RST << std::endl;
        }
    }
    if (extracted == 0) {
        std::cout << YELL "    解包完成，未找到文本文件" RST << std::endl;
    }
    archive_read_free(a);
}
#endif

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
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

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
    auto all_vars = extract_all_vars(pkgbuild);
    if (pkgname.empty()) return files;

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
                fname = resolve_install_name(fname, all_vars);
                std::string furl = "https://aur.archlinux.org/cgit/aur.git/plain/" + fname + "?h=" + name;
                std::cout << "  正在下载: " << fname << std::endl;
                try {
                    std::string content = curl_fetch(furl);
                    files.emplace_back(fname, content);
                } catch (const std::exception& e) {
                    std::cerr << RED "    下载失败: " << fname << RST << std::endl;
                }
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
        } catch (const std::exception& e) {
            std::cerr << RED "    下载失败: " << name << ".tar.gz" RST << std::endl;
            return files;
        }
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

std::vector<std::pair<std::string, std::string>> fetch_downloaded_urls(const Config& cfg, const std::vector<std::string>& urls) {
    std::vector<std::pair<std::string, std::string>> results;

    std::cout << "  正在下载外部源码..." << std::endl;
    int file_count = 0;
    const int max_downloads = 5;

    for (size_t ui = 0; ui < urls.size() && ui < (size_t)max_downloads; ui++) {
        const std::string& url = urls[ui];
        double size_mb = 0;
        std::string data;

        std::cout << "    下载: " << url << std::endl;
        try {
            popen_fetch_progress(url, data, size_mb);
            std::cerr << "\r\033[K     \033[32m完成\033[0m: " << fmt_size(size_mb * 1024 * 1024) << std::endl;
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (msg.find("too large") != std::string::npos) {
                std::cerr << "\r\033[K     " YELL "跳过: " << msg << RST << std::endl;
            } else {
                std::cerr << "\r\033[K     " RED "下载失败: " << url << RST << std::endl;
            }
            continue;
        }

        if (data.empty()) continue;

        // Skip precompiled binary packages
        std::string skip_exts[] = {".pkg.tar.", ".deb", ".rpm", ".AppImage"};
        bool skip = false;
        for (const auto& e : skip_exts)
            if (url.find(e) != std::string::npos) { skip = true; break; }
        if (skip) {
            std::string fname = url.substr(url.find_last_of('/') + 1);
            std::cout << "    文件: " << fname << " (" << fmt_size(data.size()) << ") " YELL "跳过（预编译包）" RST << std::endl;
            continue;
        }

        if (is_archive_ext(url)) {
#ifdef HAVE_LIBARCHIVE
            std::cout << "    解包: " << url.substr(url.find_last_of('/') + 1) << std::endl;
            extract_from_memory(data, results, file_count, 10 - static_cast<int>(results.size()), "source");
#else
            std::cout << YELL "    跳过解包（需要 libarchive）" RST << std::endl;
#endif
        } else {
            // Single file from URL
            if (file_count >= 10) break;
            std::string fname = url.substr(url.find_last_of('/') + 1);
            std::string size_str = fmt_size(data.size());

            if (!is_text_ext(fname)) {
                std::cout << "    文件: " << fname << " (" << size_str << ") " YELL "跳过（非文本）" RST << std::endl;
                continue;
            }

            bool binary = false;
            for (char c : data) {
                if (c == 0) { binary = true; break; }
            }
            if (binary || data.size() > 102400) {
                std::cout << "    文件: " << fname << " (" << size_str << ") " YELL "跳过（" << (binary ? "二进制" : "过大") << "）" RST << std::endl;
            } else {
                std::cout << "    文件: " << fname << " (" << size_str << ") " GREEN "通过" RST << std::endl;
                results.emplace_back("source/" + fname, data);
                file_count++;
            }
        }
    }

    return results;
}
