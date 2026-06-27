#include "config.h"
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#ifdef HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

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

static std::string read_machine_id() {
    std::ifstream f("/etc/machine-id");
    if (!f.is_open()) return {};
    std::string id;
    std::getline(f, id);
    return id;
}

static std::string base64_encode(const std::string& in) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    BIO_write(bio, in.data(), static_cast<int>(in.size()));
    BIO_flush(bio);
    char* out;
    long len = BIO_get_mem_data(bio, &out);
    std::string result(out, len);
    BIO_free_all(bio);
    return result;
}

static std::string base64_decode(const std::string& in) {
    BIO* bio = BIO_new_mem_buf(in.data(), static_cast<int>(in.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    std::string result(in.size(), '\0');
    int len = BIO_read(bio, result.data(), static_cast<int>(result.size()));
    BIO_free_all(bio);
    if (len < 0) return {};
    result.resize(len);
    return result;
}

static std::pair<std::string, std::string> encrypt_aes(const std::string& plain) {
    std::string mid = read_machine_id();
    if (mid.empty()) return {};

    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) return {};

    std::string salt_str(reinterpret_cast<char*>(salt), sizeof(salt));
    std::string kdf_input = mid + salt_str;

    unsigned char aes_key[32];
    SHA256(reinterpret_cast<const unsigned char*>(kdf_input.data()),
           kdf_input.size(), aes_key);

    unsigned char iv[16];
    std::string iv_input(reinterpret_cast<char*>(aes_key), 32);
    iv_input += salt_str;
    unsigned char hash2[32];
    SHA256(reinterpret_cast<const unsigned char*>(iv_input.data()),
           iv_input.size(), hash2);
    memcpy(iv, hash2, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    int out_len = static_cast<int>(plain.size()) + 16;
    std::string cipher(out_len, '\0');
    int written = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aes_key, iv);
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipher.data()),
                      &out_len,
                      reinterpret_cast<const unsigned char*>(plain.data()),
                      static_cast<int>(plain.size()));
    written = out_len;
    EVP_EncryptFinal_ex(ctx,
                        reinterpret_cast<unsigned char*>(cipher.data()) + written,
                        &out_len);
    written += out_len;
    cipher.resize(written);
    EVP_CIPHER_CTX_free(ctx);

    return {base64_encode(cipher), base64_encode(salt_str)};
}

static std::string decrypt_aes(const std::string& cipher_b64, const std::string& salt_b64) {
    std::string mid = read_machine_id();
    if (mid.empty()) return {};

    std::string cipher = base64_decode(cipher_b64);
    std::string salt_str = base64_decode(salt_b64);
    if (cipher.empty() || salt_str.size() != 16) return {};

    std::string kdf_input = mid + salt_str;
    unsigned char aes_key[32];
    SHA256(reinterpret_cast<const unsigned char*>(kdf_input.data()),
           kdf_input.size(), aes_key);

    unsigned char iv[16];
    std::string iv_input(reinterpret_cast<char*>(aes_key), 32);
    iv_input += salt_str;
    unsigned char hash2[32];
    SHA256(reinterpret_cast<const unsigned char*>(iv_input.data()),
           iv_input.size(), hash2);
    memcpy(iv, hash2, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    int out_len = static_cast<int>(cipher.size());
    std::string plain(out_len, '\0');

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aes_key, iv);
    EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()),
                      &out_len,
                      reinterpret_cast<const unsigned char*>(cipher.data()),
                      static_cast<int>(cipher.size()));
    int written = out_len;
    EVP_DecryptFinal_ex(ctx,
                        reinterpret_cast<unsigned char*>(plain.data()) + written,
                        &out_len);
    written += out_len;
    plain.resize(written);
    EVP_CIPHER_CTX_free(ctx);

    return plain;
}

#ifdef HAVE_LIBSECRET
static const SecretSchema* aursec_schema() {
    static const SecretSchema schema = {
        "com.github.aursec",
        SECRET_SCHEMA_NONE,
        {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
    };
    return &schema;
}
#endif

bool detect_keyring() {
#ifdef HAVE_LIBSECRET
    GError* error = nullptr;
    SecretService* ss = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
    if (error) {
        g_error_free(error);
        return false;
    }
    g_object_unref(ss);
    return true;
#else
    return false;
#endif
}

static void keyring_store(const std::string& key) {
#ifdef HAVE_LIBSECRET
    GError* error = nullptr;
    secret_password_store_sync(aursec_schema(), SECRET_COLLECTION_DEFAULT,
                               "aursec API Key", key.c_str(),
                               nullptr, &error,
                               "key", "api_key",
                               nullptr);
    if (error) {
        g_warning("Failed to store in keyring: %s", error->message);
        g_error_free(error);
    }
#endif
}

static std::string keyring_lookup() {
#ifdef HAVE_LIBSECRET
    GError* error = nullptr;
    gchar* val = secret_password_lookup_sync(aursec_schema(), nullptr, &error,
                                              "key", "api_key",
                                              nullptr);
    if (error) {
        g_error_free(error);
        return {};
    }
    if (!val) return {};
    std::string result(val);
    secret_password_free(val);
    return result;
#else
    return {};
#endif
}

static void keyring_clear() {
#ifdef HAVE_LIBSECRET
    GError* error = nullptr;
    secret_password_clear_sync(aursec_schema(), nullptr, &error,
                                "key", "api_key",
                                nullptr);
    if (error) {
        g_error_free(error);
    }
#endif
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
            if (j.contains("review_level")) cfg.review_level = j["review_level"].get<std::string>();
            if (j.contains("key_cipher")) cfg.key_cipher = j["key_cipher"].get<std::string>();
            if (j.contains("key_salt")) cfg.key_salt = j["key_salt"].get<std::string>();

            std::string em;
            if (j.contains("enc_method")) em = j["enc_method"].get<std::string>();
            if (em == "cipher") cfg.enc_method = EncMethod::Cipher;
            else if (em == "keyring") cfg.enc_method = EncMethod::Keyring;
        } catch (...) {}
    }

    if (cfg.enc_method == EncMethod::Keyring) {
        std::string k = keyring_lookup();
        if (!k.empty()) cfg.api_key = k;
    } else if (cfg.enc_method == EncMethod::Cipher) {
        if (!cfg.key_cipher.empty() && !cfg.key_salt.empty()) {
            std::string k = decrypt_aes(cfg.key_cipher, cfg.key_salt);
            if (!k.empty()) cfg.api_key = k;
        }
    }

    cfg.api_key = get_env("DEEPSEEK_API_KEY", cfg.api_key);
    cfg.base_url = get_env("DEEPSEEK_BASE_URL", cfg.base_url);
    cfg.model = get_env("AI_MODEL", cfg.model);
    cfg.loaded = !cfg.api_key.empty();
    return cfg;
}

bool detect_libarchive() {
    void* h = dlopen("libarchive.so", RTLD_LAZY | RTLD_NOLOAD);
    if (h) { dlclose(h); return true; }
    h = dlopen("libarchive.so.13", RTLD_LAZY | RTLD_NOLOAD);
    if (h) { dlclose(h); return true; }
    return false;
}

void save_config(const Config& cfg) {
    std::string dir = config_dir();
    mkdir(dir.c_str(), 0755);

    Config to_save = cfg;

    if (cfg.enc_method == EncMethod::Keyring) {
        keyring_store(cfg.api_key);
        to_save.api_key.clear();
        to_save.key_cipher.clear();
        to_save.key_salt.clear();
    } else if (cfg.enc_method == EncMethod::Cipher) {
        auto [cipher, salt] = encrypt_aes(cfg.api_key);
        to_save.api_key.clear();
        to_save.key_cipher = cipher;
        to_save.key_salt = salt;
    }

    nlohmann::json j;
    if (!to_save.api_key.empty()) j["api_key"] = to_save.api_key;
    j["base_url"] = to_save.base_url;
    j["model"] = to_save.model;
    if (!to_save.prompt_file.empty()) j["prompt_file"] = to_save.prompt_file;
    j["review_level"] = cfg.review_level;

    switch (cfg.enc_method) {
        case EncMethod::Cipher:
            j["enc_method"] = "cipher";
            j["key_cipher"] = to_save.key_cipher;
            j["key_salt"] = to_save.key_salt;
            break;
        case EncMethod::Keyring:
            j["enc_method"] = "keyring";
            break;
        default:
            j["enc_method"] = "plain";
            break;
    }

    std::ofstream f(config_path());
    f << j.dump(2) << std::endl;
    chmod(config_path().c_str(), 0600);
}
