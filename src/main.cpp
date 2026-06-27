#include "config.h"
#include "arg_parser.h"
#include "pkgbuild.h"
#include "ai_reviewer.h"
#include "executor.h"
#include <curl/curl.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <csignal>

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

static int run_init() {
    auto existing = load_config();
    std::cout << "=== install_aur 初始化配置 ===" << std::endl;

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
        std::cout << "配置已保存到 ~/.config/install_aur/config.json" << std::endl;

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

    if (argc < 2) {
        // yay without args shows TUI, pass through
        return exec_yay({argv[0], nullptr});
    }

    auto parsed = parse_args(argc, argv);

    if (parsed.type == OpType::Init) {
        int ret = run_init();
        curl_global_cleanup();
        return ret;
    }

    if (parsed.type != OpType::Install) {
        curl_global_cleanup();
        return exec_yay(parsed.yay_argv);
    }

    if (parsed.no_ai || parsed.packages.empty()) {
        curl_global_cleanup();
        return exec_yay(parsed.yay_argv);
    }

    Config cfg = load_config();
    if (!cfg.loaded && !parsed.no_ai) {
        std::cerr << "错误: 未配置 API Key。请运行 install_aur --init 配置，或使用 --no-ai 跳过审查" << std::endl;
        curl_global_cleanup();
        return 1;
    }

    std::cout << "正在下载 PKGBUILD..." << std::endl;
    auto pkgs = fetch_pkgbuilds(parsed.packages);

    std::vector<std::pair<std::string, std::string>> to_review;
    for (const auto& p : pkgs) {
        if (p.success) {
            to_review.emplace_back(p.name, p.content);
        } else {
            std::cout << "  " << p.name << ": 不在 AUR 中，跳过审查" << std::endl;
        }
    }

    if (to_review.empty()) {
        std::cout << "所有包均不在 AUR 中，跳过 AI 审查" << std::endl;
        curl_global_cleanup();
        return exec_yay(parsed.yay_argv);
    }

    std::cout << "正在 AI 审查 " << to_review.size() << " 个 PKGBUILD..." << std::endl;
    try {
        auto result = review_pkgbuilds(cfg, to_review);
        if (result.passed) {
            std::cout << "审查通过: " << result.reason << std::endl;
            curl_global_cleanup();
            return exec_yay(parsed.yay_argv);
        } else {
            std::cerr << "审查拒绝: " << result.reason << std::endl;
            curl_global_cleanup();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "AI 审查失败: " << e.what() << std::endl;
        std::cerr << "使用 --no-ai 跳过审查直接安装" << std::endl;
        curl_global_cleanup();
        return 1;
    }
}
