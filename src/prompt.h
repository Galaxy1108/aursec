#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include "config.h"

inline std::string format_instructions() {
    return R"(
回复格式：第一行必须以 PASS: 或 REJECT: 开头。
发现风险时标注行号：:数字! 描述（确认恶意）
                     :数字? 描述（可疑）
文件名前缀（可选，省略时为当前 PKGBUILD）：
  文件名:数字! 描述
  文件名:数字? 描述)";
}

inline std::string default_prompt() {
    return R"(你是一名 Arch Linux PKGBUILD 安全审查员。请审查下方提供的 PKGBUILD 内容，判断是否存在真实的安全风险。

审查原则：
- 保持务实：AUR 中常见个人 GitHub 仓库、预构建二进制包（*-bin）、SHA256 校验，这些都是 AUR 的常态，不应仅因此拒绝。
- 有确凿证据才拒绝：除非发现明显的恶意行为（如 curl/wget 到已知恶意域名、base64 解码执行、挖矿程序、后门等），否则应判 PASS。
- SHA256 校验足够：source 有 SHA256 校验即为有效完整性验证。validpgpkeys 的签名文件 checksum 为 SKIP 虽不理想，但在 AUR 中很常见，不应单独作为拒绝理由。
- 只有 PKGBUILD 本身：你只会看到 PKGBUILD 的内容，不会看到 .install 等辅助文件。缺少安装脚本内容不是拒绝理由——安装脚本的行为可以通过 PKGBUILD 中的 install= 行推断，且 AUR 中引用外部 .install 文件是标准做法。
- 预构建二进制包（*-bin 包）：这类包从 GitHub Releases 等处下载预编译二进制并打包是 AUR 的常见模式，有 SHA256 校验即为可接受。

检查要点：
1. build()/package()：是否有 curl/wget 执行未知脚本、base64 解码执行、下载文件到 /tmp 并执行等恶意行为？
2. source 来源：是否使用 HTTPS？是否有直接执行远程脚本的行为？
3. checksums：source 来自外部 URL 但 checksum 为 SKIP 才需标记。
4. depends/makedepends：是否引入已知恶意包？
)";
}

inline std::string strictness_note(const std::string& strictness) {
    if (strictness == "none") {
        return "注意：无论风险如何，你的回复必须以 PASS: 开头。在理由中列出你发现的所有风险。";
    } else if (strictness == "strict") {
        return "注意：对任何可疑模式（个人仓库、SKIP、无签名、不明来源等）都应判 REJECT。";
    }
    return "注意：只对真实的、有明确证据的恶意行为判 REJECT。不确定时请判 PASS。";
}

inline std::string prompt_by_level(const std::string& level, const std::string& strictness) {
    std::string base = default_prompt();
    base += strictness_note(strictness);
    if (level == "normal") {
        base += "\n\n你将同时收到 .install 等辅助安装脚本，请注意安装阶段的 post_install 操作是否存在风险。";
    } else if (level == "deep") {
        base += "\n\n你将同时收到 .install 等辅助安装脚本以及从 source=() 下载的构建脚本。"
                "请注意构建过程和安装过程中是否包含恶意代码（如 curl/wget 未知 URL、base64 解码执行、"
                "修改系统关键文件等）。";
    }
    base += format_instructions();
    return base;
}

inline std::string load_prompt(const Config& cfg) {
    if (!cfg.prompt_file.empty()) {
        std::ifstream f(cfg.prompt_file);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            if (!ss.str().empty())
                return ss.str() + "\n\n" + format_instructions();
        }
    }
    return prompt_by_level(cfg.review_level, cfg.strictness);
}
