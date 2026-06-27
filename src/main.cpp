#include "config.h"
#include "arg_parser.h"
#include "pkgbuild.h"
#include "ai_reviewer.h"
#include "executor.h"
#include <curl/curl.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sstream>
#include <csignal>

#define RST   "\033[0m"
#define GREEN "\033[32m"
#define RED   "\033[31m"
#define YELL  "\033[33m"
#define CYAN  "\033[36m"

static std::string read_hidden(const std::string& prompt) {
    std::cout << prompt << std::flush;
    termios old;
    tcgetattr(STDIN_FILENO, &old);
    termios no_echo = old;
    no_echo.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &no_echo);
    std::string input;
    std::getline(std::cin, input);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::cout << std::endl;
    return input;
}

static std::string read_line(const std::string& prompt, const std::string& default_val) {
    std::cout << prompt << " [" << default_val << "]: " << std::flush;
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return default_val;
    return input;
}

static int select_option(const std::vector<std::string>& options, const std::string& prompt) {
    for (size_t i = 0; i < options.size(); i++) {
        std::cout << "  " << (i + 1) << ". " << options[i] << std::endl;
    }
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    try {
        int choice = std::stoi(input);
        if (choice >= 1 && choice <= static_cast<int>(options.size())) {
            return choice - 1;
        }
    } catch (...) {}
    return -1;
}

static std::vector<std::string> parse_outdated_packages(const std::string& output) {
    std::vector<std::string> pkgs;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string name;
        ls >> name;
        if (name.empty()) continue;
        size_t slash = name.find('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        pkgs.push_back(name);
    }
    return pkgs;
}

static int run_init() {
    auto existing = load_config();
    std::cout << "=== aursec 初始化配置 ===" << std::endl;

    std::string key = read_hidden("DeepSeek API Key: ");
    if (key.empty()) {
        std::cerr << "API Key 不能为空" << std::endl;
        return 1;
    }

    Config tmp;
    tmp.api_key = key;
    if (!existing.base_url.empty()) tmp.base_url = existing.base_url;

    std::cout << "正在验证 API Key..." << std::endl;
    try {
        std::vector<std::string> models = list_models(tmp);
        std::cout << "验证成功！可用模型：" << std::endl;
        int sel = select_option(models, "请选择模型 (输入编号): ");
        if (sel < 0) {
            std::cerr << "无效选择" << std::endl;
            return 1;
        }
        tmp.model = models[sel];

        std::string base = read_line("Base URL", tmp.base_url);
        tmp.base_url = base;

        save_config(tmp);
        std::cout << "配置已保存到 ~/.config/aursec/config.json" << std::endl;

        std::cout << "正在发送测试审查请求..." << std::endl;
        auto result = review_pkgbuilds(tmp, {{"test", "source=(https://example.com/test.tar.gz)\nsha256sums=('SKIP')\npackage() { true; }"}});
        if (result.passed) {
            std::cout << "测试通过！" << std::endl;
        } else {
            std::cout << "测试完成。" << std::endl;
        }
        std::cout << "初始化完成！" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "验证失败: " << e.what() << std::endl;
        std::cerr << "请检查 API Key 和网络连接后重试" << std::endl;
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);

    auto parsed = parse_args(argc, argv);

    if (parsed.type == OpType::Init) {
        int ret = run_init();
        curl_global_cleanup();
        return ret;
    }

    if (parsed.type == OpType::Version) {
        std::cout << "aursec " << AURSEC_VERSION << std::endl;
        curl_global_cleanup();
        return 0;
    }

    if (parsed.type != OpType::Install) {
        curl_global_cleanup();
        return exec_yay(parsed.yay_argv);
    }

    if (parsed.no_ai) {
        curl_global_cleanup();
        return exec_yay(parsed.yay_argv);
    }

    if (parsed.packages.empty()) {
        if (parsed.is_upgrade) {
            std::cout << "正在查询可更新的 AUR 包..." << std::endl;
            std::vector<const char*> qargv = {"yay", "-Qua", nullptr};
            try {
                std::string output = exec_capture(qargv);
                std::vector<std::string> outdated = parse_outdated_packages(output);
                if (outdated.empty()) {
                    std::cout << "没有需要更新的 AUR 包" << std::endl;
                } else {
                    std::cout << "发现 " << outdated.size() << " 个可更新 AUR 包" << std::endl;
                    parsed.packages = std::move(outdated);
                }
            } catch (const std::exception& e) {
                std::cerr << RED "查询失败: " << e.what() << RST << std::endl;
                std::cerr << "网络不可用，建议使用 --no-ai 跳过审查" << std::endl;
                curl_global_cleanup();
                return 1;
            }
        }
        if (parsed.packages.empty()) {
            curl_global_cleanup();
            return exec_yay(parsed.yay_argv);
        }
    }

    Config cfg = load_config();
    if (!cfg.loaded) {
        std::cerr << "错误: 未配置 API Key。请运行 aursec --init 配置，或使用 --no-ai 跳过审查" << std::endl;
        curl_global_cleanup();
        return 1;
    }

    std::cout << CYAN "正在下载 PKGBUILD..." RST << std::endl;
    auto pkgs = fetch_pkgbuilds(parsed.packages);

    std::vector<std::string> approved;
    std::vector<std::string> rejected;
    int net_errors = 0;

    for (const auto& p : pkgs) {
        if (p.status == DownloadStatus::NotFound) {
            std::cout << "  " << p.name << ": 不在 AUR 中，跳过审查" << std::endl;
            approved.push_back(p.name);
            continue;
        }

        if (p.status == DownloadStatus::NetworkError) {
            std::cerr << YELL "  " << p.name << ": 下载失败（网络不可用），已阻止" RST << std::endl;
            rejected.push_back(p.name);
            net_errors++;
            continue;
        }

        std::cout << "正在 AI 审查 " << p.name << "..." << std::endl;
        try {
            auto result = review_pkgbuilds(cfg, {{p.name, p.content}});
            if (result.passed) {
                std::cout << GREEN "  " << p.name << ": 通过 - " << result.reason << RST << std::endl;
                approved.push_back(p.name);
            } else {
                std::cout << RED "  " << p.name << ": 拒绝 - " << result.reason << RST << std::endl;
                rejected.push_back(p.name);
            }
        } catch (const std::exception& e) {
            std::cerr << RED "  " << p.name << ": 审查失败 - " << e.what() << RST << std::endl;
            rejected.push_back(p.name);
        }
    }

    if (approved.empty()) {
        if (net_errors == static_cast<int>(pkgs.size())) {
            std::cerr << RED "网络不可用，所有包下载失败。建议使用 --no-ai 跳过审查" RST << std::endl;
        } else {
            std::cerr << RED "所有包均未通过 AI 审查，退出" RST << std::endl;
        }
        curl_global_cleanup();
        return 1;
    }

    if (!rejected.empty()) {
        std::cout << YELL << rejected.size() << " 个包被跳过，继续安装通过审查的包" RST << std::endl;
    }

    std::vector<const char*> new_argv;
    new_argv.push_back(parsed.yay_argv[0]);
    for (size_t i = 1; parsed.yay_argv[i] != nullptr; i++) {
        std::string arg = parsed.yay_argv[i];
        if (arg == "-Su" || arg == "-Syu" || arg == "--sysupgrade") {
            new_argv.push_back("-S");
        } else if (!arg.empty() && arg[0] == '-') {
            new_argv.push_back(parsed.yay_argv[i]);
        }
    }
    for (const auto& pkg : approved) {
        new_argv.push_back(pkg.c_str());
    }
    new_argv.push_back(nullptr);

    curl_global_cleanup();
    return exec_yay(new_argv);
}
