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

static std::string popen_fetch(const std::string& url, int timeout_sec = 15) {
    std::string cmd = "curl -sS -L --connect-timeout " + std::to_string(timeout_sec / 2)
        + " --max-time " + std::to_string(timeout_sec) + " -o- " + url;
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

static std::string popen_fetch_with_size(const std::string& url, double& size_mb) {
    std::string body = popen_fetch(url, 30);
    size_mb = (double)body.size() / (1024.0 * 1024.0);
    if (body.size() > 5 * 1024 * 1024) {
        body.clear();
        throw std::runtime_error("file too large (>5MB)");
    }
    return body;
}

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

    // Try .install file
    {
        std::string url = "https://aur.archlinux.org/cgit/aur.git/plain/" + pkgname + ".install?h=" + name;
        std::cout << "  正在下载: " << pkgname << ".install" << std::endl;
        try {
            std::string content = curl_fetch(url);
            files.emplace_back(pkgname + ".install", content);
        } catch (const std::exception& e) {
            std::cerr << RED "    下载失败: " << pkgname << ".install" RST << std::endl;
        }
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

static std::string bash_resolve(const std::string& raw, const std::string& pkgbuild) {
    std::string script;
    std::istringstream stream(pkgbuild);
    std::string line;
    while (std::getline(stream, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string k = line.substr(0, eq);
        bool valid = true;
        for (char c : k)
            if (!isalnum(c) && c != '_') { valid = false; break; }
        if (!valid || k == "source" || k.find("source_") == 0 || k.find("sha256sums") == 0) continue;

        std::string v = line.substr(eq + 1);
        // Skip assignments with command substitution (security)
        if (v.find("$(") != std::string::npos || v.find('`') != std::string::npos) continue;
        script += line + "\n";
    }

    std::string r = raw;
    size_t p = 0;
    while ((p = r.find('\\', p)) != std::string::npos) { r.replace(p, 1, "\\\\"); p += 2; }
    p = 0; while ((p = r.find('"', p)) != std::string::npos) { r.replace(p, 1, "\\\""); p += 2; }
    p = 0; while ((p = r.find("$(", p)) != std::string::npos) { r.replace(p, 2, "\\$("); p += 2; }
    p = 0; while ((p = r.find('`', p)) != std::string::npos) { r.replace(p, 1, "\\`"); p += 2; }
    script += "echo \"" + r + "\"";

    std::string tmp = "/tmp/aursec_resolve_" + std::to_string(getpid()) + ".sh";
    {
        std::ofstream f(tmp);
        f << "#!/bin/bash\n" << script << "\n";
    }
    chmod(tmp.c_str(), 0755);

    FILE* pipe = popen(("timeout 3 bash " + tmp + " 2>/dev/null").c_str(), "r");
    if (!pipe) { unlink(tmp.c_str()); return raw; }

    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    int status = pclose(pipe);
    unlink(tmp.c_str());

    if (result.empty() && status != 0) return raw;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result.empty() ? raw : result;
}

std::vector<std::pair<std::string, std::string>> fetch_source_urls(const std::string& pkgbuild) {
    std::vector<std::pair<std::string, std::string>> results;

    // Parse source=() blocks
    std::vector<std::string> raw_urls;
    std::regex source_block(R"(source(?:_\w+)?=\(\s*([^)]*?)\s*\))");
    std::sregex_iterator it(pkgbuild.begin(), pkgbuild.end(), source_block), end;
    for (; it != end; ++it) {
        std::string body = (*it)[1].str();
        std::regex entry_re(R"(['\"]((?:[^'\"\\]|\\.)*)['\"])");
        std::sregex_iterator eit(body.begin(), body.end(), entry_re);
        for (; eit != std::sregex_iterator(); ++eit) {
            std::string entry = (*eit)[1].str();
            // Resolve all bash variables and parameter expansions
            entry = bash_resolve(entry, pkgbuild);
            // Handle "filename::URL" format
            size_t sep = entry.find("::");
            std::string url = (sep != std::string::npos) ? entry.substr(sep + 2) : entry;
            if (url.find("http://") == 0 || url.find("https://") == 0)
                raw_urls.push_back(url);
        }
    }

    // Deduplicate
    std::sort(raw_urls.begin(), raw_urls.end());
    raw_urls.erase(std::unique(raw_urls.begin(), raw_urls.end()), raw_urls.end());

    if (raw_urls.empty()) return results;

    std::cout << "  正在下载外部源码..." << std::endl;
    int file_count = 0;
    const int max_downloads = 5;

    for (size_t ui = 0; ui < raw_urls.size() && ui < (size_t)max_downloads; ui++) {
        const std::string& url = raw_urls[ui];
        double size_mb = 0;
        std::string data;

        try {
            std::cout << "    下载: " << url << std::endl;
            data = popen_fetch_with_size(url, size_mb);
            std::cout << "    大小: " << fmt_size(size_mb * 1024 * 1024) << std::endl;
        } catch (const std::exception& e) {
            std::cerr << RED "    下载失败: " << url << RST << std::endl;
            continue;
        }

        if (data.empty()) continue;

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
