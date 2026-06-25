---
title: CI 与质量检查
description: 面向开源协作的文档站、前端、C++ 后端、静态分析和发布构建检查入口。
prev:
  text: 后端开发
  link: /development/backend
next: false
---

# CI 与质量检查

本文整理当前仓库中已经存在、可以逐步接入 CI 的质量检查入口。正式公开前，建议先把轻量检查放入 GitHub Actions，把依赖硬件或耗时较长的检查保留为手动工作流或 self-hosted runner。

## 推荐检查分层

| 层级 | 检查项 | 建议触发方式 |
| --- | --- | --- |
| 文档站 | `npm ci`、`npm run docs:build` | Pull request / push |
| 前端 | `npm ci`、`npm run i18n:check`、`npm run build`、`npm run resource-i18n:check` | Pull request / push |
| C++ 格式 | `scripts/format_check.sh --check` | Pull request / push |
| C++ 静态分析 | `scripts/static_analysis.sh --cppcheck`、`scripts/static_analysis.sh --clang-tidy` | 定期 / 手动 / self-hosted |
| CPU 测试构建 | `scripts/build_cpu_test.sh`、`build_cpu/cosmo-tests` | Pull request / 手动 |
| x86 Docker | `docker compose -f docker-compose.x86.yml up -d --build` (Windows 下为 `docker-compose.x86.windows.yml`) | 手动 / release 前 |
| Sophon 发布包 | `scripts/build_sophon_package.sh` | 手动 / self-hosted |

## 文档站检查

根目录 `package.json` 用于 VitePress 文档站：

```bash
npm ci
npm run docs:build
```

本地预览：

```bash
npm run docs:preview
```

说明：

- 文档站构建会检查 VitePress 页面、导航和站内链接。
- 仓库已存在 `.github/workflows/docs.yml`（手动触发 `workflow_dispatch`，构建 VitePress 并部署到 GitHub Pages）；上表"文档站"层的 `npm run docs:build` 可作为该 workflow 的基础或在其上扩展。
- 当前依赖审计可能报告 npm dependency vulnerabilities，公开发布前应单独评估并记录处理结论。

## 前端检查

前端工程位于 `src/web`，并包含独立的 `package-lock.json`：

```bash
cd src/web
npm ci
npm run i18n:check
npm run build
npm run resource-i18n:check
```

说明：

- `npm run build` 会先通过 `prebuild` 自动执行 `npm run i18n:check`。
- `resource-i18n:check` 用于检查资源类国际化内容是否同步。
- 如果修改了资源文本，可先运行 `npm run resource-i18n:sync`，再复查 diff。

## C++ 格式检查

仓库提供 `scripts/format_check.sh`：

```bash
bash scripts/format_check.sh --check
```

仅检查暂存区文件：

```bash
bash scripts/format_check.sh --staged --check
```

自动格式化：

```bash
bash scripts/format_check.sh --fix
```

说明：

- 脚本检查 `src` 和 `test` 下的 `.h` / `.cc` 文件。
- 需要本机安装 `clang-format`。
- `3rd`、`build` 等目录会被排除。

## C++ 静态分析

仓库提供 `scripts/static_analysis.sh`：

```bash
bash scripts/static_analysis.sh --cppcheck
bash scripts/static_analysis.sh --clang-tidy
bash scripts/static_analysis.sh --all
```

说明：

- `cppcheck` 适合先接入 CI，覆盖 warning、style、performance、portability 等类别。
- `clang-tidy` 依赖 `build/compile_commands.json`，需要先完成对应构建配置。
- `--summary` 可从 `build.log` 中汇总常见编译告警。

## CPU 测试构建

CPU 测试构建脚本：

```bash
bash scripts/build_cpu_test.sh
```

脚本会配置 `build_cpu`，启用 `BUILD_TESTS=ON`，并构建：

```text
build_cpu/cosmo-tests
```

构建完成后可运行：

```bash
./build_cpu/cosmo-tests
```

说明：

- 该路径使用 x86 CPU backend 和 ONNX Runtime。
- 脚本会生成或链接 `compile_commands.json`，方便 IDE 和静态分析工具使用。
- 当前脚本提示需要 `pkg-config` 和 OpenH264 development package。

## x86 Docker 验证

x86 开发模式可用于集成级验证：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  docker compose -f docker-compose.x86.yml logs -f
  docker compose -f docker-compose.x86.yml down
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  docker compose -f docker-compose.x86.windows.yml logs -f
  docker compose -f docker-compose.x86.windows.yml down
  ```

建议在 release 前至少确认：

- Web 控制台可以访问。
- 核心服务进程正常启动。
- 常用端口没有冲突。
- 首次体验路径没有阻塞。

## Sophon 发布包验证

Sophon/aarch64 发布包构建入口：

```bash
bash scripts/build_sophon_package.sh
```

Windows PowerShell：

```powershell
.\scripts\build_sophon_package.ps1
```

Sophon 发布包构建依赖交叉编译环境和 Sophon SDK。`build_output/` 中导出的包名格式为 `cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz`。

