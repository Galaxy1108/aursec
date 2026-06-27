#include "../src/ai_reviewer.h"
#include "../src/arg_parser.h"
#include <cassert>
#include <iostream>
#include <string>

static void test_parse_review_pass() {
    auto r = parse_review_response("PASS: 所有 source 均来自官方源，checksum 完整，无危险操作");
    assert(r.passed);
    assert(!r.reason.empty());
    std::cout << "  PASS parse test: OK" << std::endl;
}

static void test_parse_review_reject() {
    auto r = parse_review_response("REJECT: source 包含不明 URL，checksum 为 SKIP，存在安全风险");
    assert(!r.passed);
    assert(!r.reason.empty());
    std::cout << "  REJECT parse test: OK" << std::endl;
}

static void test_parse_review_pas_lowercase() {
    auto r = parse_review_response("pass: source are all official");
    assert(r.passed);
    std::cout << "  lowercase pass test: OK" << std::endl;
}

static void test_parse_review_unknown() {
    auto r = parse_review_response("I am not sure what this is");
    assert(!r.passed);
    assert(r.reason.find("unexpected") != std::string::npos);
    std::cout << "  unknown format test: OK" << std::endl;
}

static void test_parse_review_without_colon() {
    auto r = parse_review_response("PASS clean and safe");
    assert(r.passed);
    assert(r.reason == "clean and safe");
    std::cout << "  PASS without colon test: OK" << std::endl;
}

static void test_parse_args_init() {
    const char* av[] = {"aursec", "--init", nullptr};
    auto r = parse_args(2, const_cast<char**>(av));
    assert(r.type == OpType::Init);
    std::cout << "  --init test: OK" << std::endl;
}

static void test_parse_args_no_ai() {
    const char* av[] = {"aursec", "--no-ai", "-S", "firefox", nullptr};
    auto r = parse_args(4, const_cast<char**>(av));
    assert(r.no_ai);
    assert(r.type == OpType::Install);
    assert(r.packages.size() == 1);
    assert(r.packages[0] == "firefox");
    std::cout << "  --no-ai with -S test: OK" << std::endl;
}

static void test_parse_args_Syu() {
    const char* av[] = {"aursec", "-Syu", nullptr};
    auto r = parse_args(2, const_cast<char**>(av));
    assert(r.type == OpType::Install);
    assert(r.packages.empty());
    std::cout << "  -Syu test: OK" << std::endl;
}

static void test_parse_args_Ss() {
    const char* av[] = {"aursec", "-Ss", "firefox", nullptr};
    auto r = parse_args(3, const_cast<char**>(av));
    assert(r.type == OpType::Passthru);
    std::cout << "  -Ss (search) test: OK" << std::endl;
}

static void test_parse_args_bare_name() {
    const char* av[] = {"aursec", "firefox", nullptr};
    auto r = parse_args(2, const_cast<char**>(av));
    assert(r.type == OpType::Install);
    assert(r.packages.size() == 1);
    assert(r.packages[0] == "firefox");
    std::cout << "  bare package name test: OK" << std::endl;
}

static void test_parse_args_Qi() {
    const char* av[] = {"aursec", "-Qi", "firefox", nullptr};
    auto r = parse_args(3, const_cast<char**>(av));
    assert(r.type == OpType::Passthru);
    std::cout << "  -Qi (query info) test: OK" << std::endl;
}

static void test_parse_args_R() {
    const char* av[] = {"aursec", "-R", "firefox", nullptr};
    auto r = parse_args(3, const_cast<char**>(av));
    assert(r.type == OpType::Passthru);
    std::cout << "  -R (remove) test: OK" << std::endl;
}

static void test_parse_args_multiple_pkgs() {
    const char* av[] = {"aursec", "-S", "pkg1", "pkg2", "pkg3", nullptr};
    auto r = parse_args(5, const_cast<char**>(av));
    assert(r.type == OpType::Install);
    assert(r.packages.size() == 3);
    assert(r.packages[0] == "pkg1");
    assert(r.packages[1] == "pkg2");
    assert(r.packages[2] == "pkg3");
    std::cout << "  multiple packages test: OK" << std::endl;
}

static void test_parse_args_yay_argv_passthru() {
    const char* av[] = {"aursec", "-Ss", "keyword", nullptr};
    auto r = parse_args(3, const_cast<char**>(av));
    assert(r.yay_argv.size() == 4); // program + -Ss + keyword + nullptr
    assert(std::string(r.yay_argv[1]) == "-Ss");
    assert(std::string(r.yay_argv[2]) == "keyword");
    std::cout << "  yay argv passthru test: OK" << std::endl;
}

static void test_parse_args_no_args_equals_Syu() {
    const char* av[] = {"aursec", nullptr};
    auto r = parse_args(1, const_cast<char**>(av));
    assert(r.type == OpType::Install);
    assert(r.is_upgrade);
    assert(r.packages.empty());
    assert(r.yay_argv.size() == 3); // program + -Syu + nullptr
    assert(std::string(r.yay_argv[1]) == "-Syu");
    std::cout << "  no args = -Syu test: OK" << std::endl;
}

static void test_parse_args_no_ai_passthru() {
    const char* av[] = {"aursec", "--no-ai", "-R", "firefox", nullptr};
    auto r = parse_args(4, const_cast<char**>(av));
    assert(r.no_ai);
    assert(r.type == OpType::Passthru);
    assert(r.yay_argv.size() == 4); // program + -R + firefox + nullptr
    assert(std::string(r.yay_argv[1]) == "-R");
    assert(std::string(r.yay_argv[2]) == "firefox");
    std::cout << "  --no-ai with -R passthru test: OK" << std::endl;
}

int main() {
    std::cout << "=== AI Reviewer Tests ===" << std::endl;
    test_parse_review_pass();
    test_parse_review_reject();
    test_parse_review_pas_lowercase();
    test_parse_review_unknown();
    test_parse_review_without_colon();

    std::cout << "\n=== Arg Parser Tests ===" << std::endl;
    test_parse_args_init();
    test_parse_args_no_ai();
    test_parse_args_Syu();
    test_parse_args_Ss();
    test_parse_args_bare_name();
    test_parse_args_Qi();
    test_parse_args_R();
    test_parse_args_multiple_pkgs();
    test_parse_args_yay_argv_passthru();
    test_parse_args_no_ai_passthru();
    test_parse_args_no_args_equals_Syu();

    std::cout << "\n所有测试通过!" << std::endl;
    return 0;
}
