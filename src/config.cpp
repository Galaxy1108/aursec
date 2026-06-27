#include "config.h"
#include <fstream>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

static std::string home_dir() {
    const char* home = getenv("HOME");
    if (home) return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

static std::string config_dir() {
    return home_dir() + "/.config/aursec";
}

static std::string config_path() {
    return config_dir() + "/config.json";
}

static std::string get_env(const char* key, const std::string& fallback) {
    const char* val = getenv(key);
    return val ? std::string(val) : fallback;
}

Config load_config() {
    Config cfg;
    std::ifstream f(config_path());
    if (f.is_open()) {
        try {
            auto j = nlohmann::json::parse(f);
            if (j.contains("api_key")) cfg.api_key = j["api_key"].get<std::string>();
            if (j.contains("base_url")) cfg.base_url = j["base_url"].get<std::string>();
            if (j.contains("model")) cfg.model = j["model"].get<std::string>();
            if (j.contains("prompt_file")) cfg.prompt_file = j["prompt_file"].get<std::string>();
        } catch (...) {}
    }
    cfg.api_key = get_env("DEEPSEEK_API_KEY", cfg.api_key);
    cfg.base_url = get_env("DEEPSEEK_BASE_URL", cfg.base_url);
    cfg.model = get_env("AI_MODEL", cfg.model);
    cfg.loaded = !cfg.api_key.empty();
    return cfg;
}

void save_config(const Config& cfg) {
    std::string dir = config_dir();
    mkdir(dir.c_str(), 0755);
    nlohmann::json j;
    j["api_key"] = cfg.api_key;
    j["base_url"] = cfg.base_url;
    j["model"] = cfg.model;
    if (!cfg.prompt_file.empty()) j["prompt_file"] = cfg.prompt_file;
    std::ofstream f(config_path());
    f << j.dump(2) << std::endl;
}
