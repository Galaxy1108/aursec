#pragma once
#include <string>
#include <vector>

enum class OpType {
    Init,
    Install,
    Passthru,
    Version,
    SetModel,
    ReviewFile,
    PromptFile,
    PromptDefault,
    Help,
};

struct ParseResult {
    OpType type = OpType::Passthru;
    bool no_ai = false;
    bool is_upgrade = false;
    std::vector<std::string> packages;
    std::vector<std::string> review_files;
    std::string prompt_file_opt;
    std::vector<const char*> yay_argv; // null-terminated
};

ParseResult parse_args(int argc, char* argv[]);
