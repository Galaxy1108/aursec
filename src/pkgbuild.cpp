#include "pkgbuild.h"
#include <curl/curl.h>
#include <stdexcept>

static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* buf) {
    size_t total = size * nmemb;
    buf->append(static_cast<char*>(data), total);
    return total;
}

static PkgbuildResult fetch_one(const std::string& name) {
    std::string url = "https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=" + name;
    CURL* curl = curl_easy_init();
    if (!curl) return {name, {}, false};

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

    if (res != CURLE_OK) return {name, {}, false};
    return {name, body, true};
}

std::vector<PkgbuildResult> fetch_pkgbuilds(const std::vector<std::string>& packages) {
    std::vector<PkgbuildResult> results;
    for (const auto& name : packages) {
        results.push_back(fetch_one(name));
    }
    return results;
}
