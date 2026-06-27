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

依赖: `yay`, `curl`, `nlohmann-json`, `openssl`

可选: `libsecret`（系统密钥环支持）, `libarchive`（deep 审查级别）

## 配置

```bash
aursec --init
```

交互式配置：API Key → 验证 → 选模型 → Base URL → 加密方式 → 审查级别 → 严格度 → 拒绝确认 → 保存。

### 加密方式

| 方式 | 条件 | 说明 |
|------|------|------|
| 系统密钥环 | libsecret 可用 | Key 存入受密码保护的系统密钥环 |
| AES-256-CBC | 始终可用 | Key 加密后存入 config |
| 不加密 | 始终可选择 | Key 明文存储 |

### 审查级别

| 级别 | 说明 |
|------|------|
| basic | 仅审查 PKGBUILD |
| normal | PKGBUILD + AUR 辅助文件 (.install 等) |
| deep | PKGBUILD + 辅助 + source=() 构建脚本（需 libarchive） |

### 审查严格度

| 级别 | 行为 |
|------|------|
| none | 不拦截，仅显示风险 |
| normal | 拦截确认恶意的代码（默认） |
| strict | 拦截可疑及恶意代码 |

### 拒绝确认

AI 判 REJECT 时询问是否仍要安装，`--init` 时可关闭此确认。

## 使用

```bash
aursec                    # 等效 aursec -Syu
aursec --set-strictness   # 交互式选择严格度
aursec --set-context 5    # 设置上下文行数
aursec --set-review-level # 交互式选择审查级别
aursec --review ./PKGBUILD # 审查本地 PKGBUILD
aursec --no-ai -S pkg     # 跳过审查直接安装
```

## 自定义提示词

```bash
aursec --prompt-file ~/my-prompt.txt
```

aursec 会自动追加回复格式说明，自定义文件中只需写审查规则。

## License

MIT
