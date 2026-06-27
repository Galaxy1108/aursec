# aursec

yay 的包装器，在每次安装/更新前调用 AI (DeepSeek) 审查 PKGBUILD，降低 AUR 软件包的安全风险。

```
aursec [--no-ai] [yay 参数...]
  ├── 无参数 → 等效 aursec -Syu
  ├── 非安装操作 → 直接透传 yay
  ├── --init → 交互式配置
  └── 安装操作 → AI 审查 → PASS 则安装，REJECT 则退出
```

## 安装

```bash
# 从源码构建
makepkg -f
sudo pacman -U aursec-*.pkg.tar.zst
```

依赖: `yay`, `curl`, `nlohmann-json`

## 配置

```bash
aursec --init
```

交互式输入 DeepSeek API Key → 验证 → 选择模型 → 保存。

环境变量优先级更高：`DEEPSEEK_API_KEY` / `DEEPSEEK_BASE_URL` / `AI_MODEL`

## 使用

```bash
aursec                    # 等效 aursec -Syu
aursec -Syu               # 升级所有包（AUR 包经 AI 审查）
aursec -S firefox         # 安装 firefox（经 AI 审查）
aursec --no-ai -S pkg     # 跳过审查直接安装
aursec -Ss keyword        # 搜索，透传 yay
```

## 工作原理

1. 识别安装操作（`-S`, `-Su`, `-Syu`, `-U`, 裸包名）
2. 从 AUR 下载 PKGBUILD
3. 调用 DeepSeek API 审查安全性（source 来源、checksum、危险命令等）
4. PASS → 执行 yay 安装；REJECT → 退出码 1

## License

MIT
