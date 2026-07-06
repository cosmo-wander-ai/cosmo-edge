---
title: 贡献者上手路径
description: 面向首次贡献者的本地环境、推荐流程、验证命令和 PR 前检查清单。
prev:
  text: 模型与资源
  link: /reference/models
next:
  text: 前端工程
  link: /development/frontend
---

# 贡献者上手路径

这页面向第一次参与 CosmoEdge 的开发者，目标是把“我该从哪里开始、改完以后跑什么”说清楚。完整规则仍以根目录的 [CONTRIBUTING.md](https://github.com/cosmo-wander-ai/cosmo-edge/blob/main/CONTRIBUTING.md) 为准。

## 适合第一次贡献的方向

| 方向 | 建议范围 | 主要验证 |
| --- | --- | --- |
| 文档修正 | 错别字、链接、教程补充、命令修正 | `npm run docs:build` |
| 前端小改动 | 文案、i18n、表单校验、页面交互修复 | `npm run build` |
| C++ 单点修复 | 工具函数、DTO、service 层小修、单元测试 | `scripts/build_cpu_test.sh` 和 `cosmo-tests` |
| 场景或模型说明 | 示例配置、参数解释、接入说明 | 文档构建 + 相关手动验证 |

大范围 C++ 架构调整、新算法节点、新依赖、模型运行时接入，建议先开 issue 讨论设计。

## 本地准备

建议先准备：

- Git 和 GitHub 账号。
- Node.js / npm，用于文档站和前端构建。
- Docker Desktop 或 Docker Engine，并启用 Docker Compose V2。
- C++ 后端开发需要 Bash、CMake、C++ 编译器、`pkg-config`、`clang-format`，以及 `scripts/build_cpu_test.sh` 提示的系统依赖。
- 可选：`cppcheck`，用于本地静态分析。

Windows 开发者可以优先使用 Docker 和 PowerShell 路径；C++ 原生测试脚本仍按 Bash 环境设计。

## 推荐流程

1. Fork 仓库，从 `main` 创建描述性分支，例如 `docs/contributor-guide` 或 `fix/auth-token-refresh`。
2. 先选一个小范围改动，让 PR 容易 review。
3. 修改过程中尽量跟随现有目录、命名和测试风格。
4. 提交前运行与你改动相关的最小验证命令。
5. PR 描述里写清楚改了什么、为什么改、跑过哪些检查。
6. 使用 `git commit -s` 添加 DCO sign-off。

## 验证命令速查

### 文档

```bash
npm ci
npm run docs:build
```

本地预览：

```bash
npm run docs:preview
```

### 前端

```bash
cd src/web
npm ci
npm run i18n:check
npm run build
npm run resource-i18n:check
```

如果只改了普通页面逻辑，`npm run build` 会自动先跑 `i18n:check`。如果改了资源类 i18n，再额外跑 `resource-i18n:check`。

### C++ 后端

只检查暂存区 C++ 格式：

```bash
bash scripts/format_check.sh --staged --check
```

自动修复暂存区 C++ 格式：

```bash
bash scripts/format_check.sh --staged --fix
git add -u
```

CPU 后端测试构建：

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests
```

可选静态分析：

```bash
bash scripts/static_analysis.sh --cppcheck --staged
```

### x86 Docker 运行验证

Linux:

```bash
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml ps
docker compose -f docker-compose.x86.yml down
```

Windows PowerShell / CMD:

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
docker compose -f docker-compose.x86.windows.yml down
```

启动后 Web 控制台地址为 `http://127.0.0.1:8080`。

### Git Pre-commit Hook

CosmoEdge 内置 Git pre-commit hook，可在提交前自动检查暂存区 C++ 文件的格式（`clang-format`），并在本地安装了 `cppcheck` 时运行静态分析。安装 hook 可在问题进入 CI 前拦截。

**安装 hook：**

```bash
bash scripts/install-hooks.sh
```

这会在 `.git/hooks/pre-commit` 创建指向 `scripts/pre-commit` 的符号链接，随仓库同步更新。

**Hook 检查内容：**

1. **clang-format** — 所有暂存的 `.h` 和 `.cc` 文件需符合项目风格（根目录 `.clang-format`）。
   - 格式检查失败：运行 `bash scripts/format_check.sh --staged --fix`，然后 `git add -u`。
2. **cppcheck**（可选）— 仅当本地安装了 `cppcheck` 时运行。仅在出现 error 级别问题时拦截。

**卸载 hook：**

```bash
rm .git/hooks/pre-commit
```

**手动检查（不依赖 hook）：**

```bash
# 检查暂存区文件格式
bash scripts/format_check.sh --staged --check

# 自动修复暂存区文件格式
bash scripts/format_check.sh --staged --fix
git add -u

# 对暂存区文件运行 cppcheck（需已安装）
bash scripts/static_analysis.sh --cppcheck --staged
```

## PR 前检查清单

- 改动是否聚焦在一个主题上。
- 是否更新了相关文档、示例或测试。
- 是否跑过和改动对应的验证命令。
- PR 模板的 **Verification** 是否写了实际运行的命令。
- 是否没有提交密钥、客户信息、私有 IP、私有模型权重或专有下载链接。
- 新增第三方依赖、模型、数据集或素材时，是否写清来源和许可证。
- commit 是否带有 `Signed-off-by:`。

## 常见卡点

| 问题 | 建议处理 |
| --- | --- |
| 不知道该跑全部检查还是部分检查 | 按“验证命令速查”选择和改动相关的最小集合。 |
| 没有 Sophon 设备 | 文档、前端、x86 Docker、CPU 测试构建都不要求 Sophon 硬件。 |
| `docker compose` 不存在 | 安装 Docker Compose V2，或在旧环境中使用 `docker-compose` 替换命令。 |
| C++ 依赖缺失 | 先运行 `bash scripts/build_cpu_test.sh`，按脚本报错补系统包。 |
| 格式检查失败 | 运行 `bash scripts/format_check.sh --staged --fix`，然后重新 `git add`。 |

如果修改超过 50 行或影响公共 API、部署脚本、模型格式、流水线语义，请先开 issue 说明设计，避免后续返工。
