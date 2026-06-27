# aursec

yay 的包装器，在每次安装/更新前调用 AI 审查 PKGBUILD。

## 架构

```
aursec [--no-ai] [yay 参数...]
  ├── 非安装操作 (-Ss, -Qi, -R, -F, -D 等) → 直接透传 yay, execvp 不返回
  ├── --init → 交互式配置 API Key / 模型 / Base URL
  └── 安装操作 (-S, -Su, -Syu, -U, 裸包名)
        ├── --no-ai → 跳过审查, 直接透传 yay
        ├── 无包名 (-Syu 不带具体包) → 透传 yay
        ├── 失败: curl 下载 PKGBUILD → AI 审查 (DeepSeek API)
        └── PASS → exec yay;  REJECT → exit 1
```

## 目录

```
├── CMakeLists.txt          # find_package(CURL + nlohmann_json)
├── src/
│   ├── main.cpp            # 入口, --init/安装/透传 三路分派
│   ├── config.h/.cpp       # ~/.config/aursec/config.json + env 覆盖
│   ├── arg_parser.h/.cpp   # 操作类型判断, 包名提取, --no-ai 剥离
│   ├── pkgbuild.h/.cpp     # curl 批量下载 PKGBUILD
│   ├── ai_reviewer.h/.cpp  # DeepSeek API 调用 + JSON 响应解析
│   ├── prompt.h            # 安全审查提示词 (集中管理)
│   └── executor.h/.cpp     # fork + exec yay, 透传退出码
└── tests/
    └── test_main.cpp       # 不联网测试 (parse_review_response, parse_args)
```

## 构建

```bash
cmake -B build && cmake --build build        # 完整构建
cmake --build build                           # 快速增量
./build/test_aursec                             # 运行单元测试
makepkg -f                                    # 打包为 .pkg.tar.zst
sudo pacman -U aursec-*.pkg.tar.zst             # 安装打包产物
```

依赖: `curl` (libcurl), `nlohmann-json`, `yay` — Arch 上默认都有。

## 配置

```bash
aursec --init            # 交互式: 输入 API Key → 验证 → 选模型 → 保存
```

`~/.config/aursec/config.json` | 环境变量 `DEEPSEEK_API_KEY / DEEPSEEK_BASE_URL / AI_MODEL` 优先级更高。

## 关键约束

1. 不引入 `yay` 之外的包管理依赖
2. `parse_review_response()` 可独立单元测试, 不依赖网络
3. prompt 在 `src/prompt.h` 集中管理, 调优只改一处
4. 默认不缓存审查结果, 每次安装都重新审
5. AI 不可用时 `--no-ai` 跳过审查直接装

## Arg Parser 行为

- `-S pkg` / `-Su` / `-Syu` / `-U` / `--sync` / `--sysupgrade` / 裸包名 → Install
- `-Ss` / `-Si` / `-Sl` / `-Sg` / `-Sc` / 所有 `-Q*` / `-R*` / `-F*` / `-D*` → Passthru
- `--init` → Init (优先级最高)
