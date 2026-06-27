#include "arg_parser.h"
#include <cstring>
#include <getopt.h>

static bool is_install_flag(const std::string& arg) {
    if (arg == "--sync" || arg == "--sysupgrade" || arg == "--upgrade" || arg == "-U")
        return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'S') {
        std::string rest = arg.substr(2);
        if (rest.empty()) return true;
        for (char c : rest) {
            if (c == 'u' || c == 'y' || c == 'w') return true;
        }
    }
    return false;
}

static bool is_upgrade_flag(const std::string& arg) {
    if (arg == "--sysupgrade" || arg == "--upgrade") return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'S') {
        for (char c : arg.substr(2)) {
            if (c == 'u') return true;
        }
    }
    return false;
}

static bool is_non_install_flag(const std::string& arg) {
    if (arg == "--search" || arg == "--info" || arg == "--list" ||
        arg == "--groups" || arg == "--clean" || arg == "--query" ||
        arg == "--remove" || arg == "--files" || arg == "--database" ||
        arg == "--version")
        return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'Q') return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'R') return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'F') return true;
    if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'D') return true;
    if (arg == "-h" || arg == "--help" || arg == "-V" || arg == "--version")
        return true;
    if (arg.size() >= 3 && arg[0] == '-' && arg[1] == 'S') {
        std::string r = arg.substr(2);
        if (r.find_first_not_of("silgcpq") == std::string::npos && !r.empty())
            return true;
    }
    return false;
}

static std::vector<std::string> extract_package_names(const std::vector<const char*>& argv) {
    std::vector<std::string> pkgs;
    for (size_t i = 1; i < argv.size(); i++) {
        if (!argv[i] || argv[i][0] == '\0') continue;
        if (argv[i][0] == '-') continue;
        std::string name = argv[i];
        size_t slash = name.find('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        size_t eq = name.find('=');
        if (eq != std::string::npos) name = name.substr(0, eq);
        pkgs.push_back(name);
    }
    return pkgs;
}

ParseResult parse_args(int argc, char* argv[]) {
    ParseResult result;

    if (argc < 2) {
        result.type = OpType::Install;
        result.is_upgrade = true;
        result.yay_argv = {argv[0], "-Syu", nullptr};
        return result;
    }

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--help") {
            result.type = OpType::Help;
            return result;
        }
        if (a == "--aursec-version") {
            result.type = OpType::Version;
            return result;
        }
        if (a == "--init") {
            result.type = OpType::Init;
            return result;
        }
        if (a == "--set-model") {
            result.type = OpType::SetModel;
            return result;
        }
        if (a == "--prompt-default") {
            result.type = OpType::PromptDefault;
            return result;
        }
        if (a == "--prompt-file") {
            result.type = OpType::PromptFile;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                result.prompt_file_opt = argv[i + 1];
            return result;
        }
        if (a == "--review") {
            result.type = OpType::ReviewFile;
            for (int j = i + 1; j < argc; j++) {
                std::string f = argv[j];
                if (!f.empty() && f[0] == '-') break;
                result.review_files.push_back(f);
            }
            return result;
        }
    }

    std::vector<const char*> filtered = {argv[0]};
    bool has_install = false;
    bool has_non_install = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-ai") {
            result.no_ai = true;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            if (is_install_flag(arg)) has_install = true;
            if (is_upgrade_flag(arg)) result.is_upgrade = true;
            if (is_non_install_flag(arg)) has_non_install = true;
        }
        filtered.push_back(argv[i]);
    }

    if (has_install) {
        result.type = OpType::Install;
    } else if (has_non_install) {
        result.type = OpType::Passthru;
    } else if (filtered.size() > 1) {
        for (size_t i = 1; i < filtered.size(); i++) {
            if (filtered[i] && filtered[i][0] != '\0' && filtered[i][0] != '-') {
                result.type = OpType::Install;
                break;
            }
        }
    }

    if (result.type == OpType::Install) {
        result.packages = extract_package_names(filtered);
    }

    result.yay_argv = filtered;
    result.yay_argv.push_back(nullptr);
    return result;
}
