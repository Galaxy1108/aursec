#pragma once
#include <string>

struct Config {
    std::string api_key;
    std::string base_url = "https://api.deepseek.com";
    std::string model = "deepseek-chat";
    std::string prompt_file;
    bool loaded = false;
};

Config load_config();
void save_config(const Config& cfg);
