#pragma once
#include <string>
#include <vector>

enum class EncMethod { Plain, Cipher, Keyring };

struct Config {
    std::string api_key;
    std::string base_url = "https://api.deepseek.com";
    std::string model = "deepseek-chat";
    std::string prompt_file;
    std::string review_level = "basic";
    std::string strictness = "normal";
    int context_lines = 2;
    int max_chars = 50000;
    int max_file_size_mb = 50;
    bool confirm_reject = true;
    std::vector<std::string> allowlist;
    EncMethod enc_method = EncMethod::Plain;
    std::string key_cipher;
    std::string key_salt;
    bool loaded = false;
};

Config load_config();
void save_config(const Config& cfg);
bool detect_keyring();
bool detect_libarchive();
