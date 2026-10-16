---
title: CI 与质量检查
description: 面向开源协作的文档站、前端、C++ 后端、静态分析和平台发布构建检查入口。
prev:
  text: 后端开发
  link: /development/backend
next:
  text: 智能体辅助二次开发
  link: /development/agent-assisted-development
---

# CI 与质量检查

本文整理当前仓库中已经存在、可以逐步接入 CI 的质量检查入口。正式公开前，建议先把轻量检查放入 GitHub Actions，把依赖硬件或耗时较长的检查保留为手动工作流或 self-hosted runner。

## 推荐检查分层

| 层级 | 检查项 | 建议触发方式 |
| --- | --- | --- |
| 文档站 | `npm ci`、`npm run docs:verify` | Pull request |
| 前端 | `npm ci`、`npm test`、`npm run build`、`npm run resource-i18n:check` | Pull request / push |
| C++ 格式 | `scripts/format_check.sh --check` | Pull request / push |
| C++ 静态分析 | `scripts/static_analysis.sh --cppcheck`、`scripts/static_analysis.sh --clang-tidy` | 定期 / 手动 / self-hosted |
| CPU 测试构建 | `scripts/build_cpu_test.sh`、`build_cpu/cosmo-tests` | Pull request / 手动 |
| x86 Docker | `docker compose -f docker-compose.x86.yml up -d --build` (Windows 下为 `docker-compose.x86.windows.yml`) | 手动 / release 前 |
| Sophon 发布包 | `./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package [--chip <型号>]`，支持 `bm1688` / `cv186x`（默认 `bm1688`） | 手动 / self-hosted |
| Rockchip 发布包 | `COSMO_TARGET_CHIP=<rk3576|rv1126b> docker compose -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package` | 每日 02:12（北京时间）/ 手动 |

## 文档站检查

根目录 `package.json` 用于 VitePress 文档站：

```bash
npm ci
npm run docs:verify
```

`docs:verify` 依次执行：

1. `gitee:readme:check`：检查 Gitee README 与源文件是否同步。
2. `docs:build`：构建完整 VitePress 文档站、检查站内链接，并生成基准报告页面。
3. `docs:smoke`：检查五篇中英文核心指南和两个索引，共十二页的渲染 HTML、标题、
   语言、导航、链接、图片 alt 和 frontmatter 泄漏。
4. `benchmarks:v1.1:smoke`：验证基准源码与生成报告的证据、校验和、公开数据边界及报告链接。

本地预览：

```bash
npm run docs:preview
```

说明：

- `.github/workflows/pr-checks.yml` 中的 `docs` job 会在 pull request 上执行同一条
  `npm run docs:verify` 命令。
- 教程结构和链接以构建结果为准，不再通过独立源码扫描强制固定章节措辞或禁用词。
- 基准完整验证已经覆盖源码，因此不在构建前重复执行 `--source-only`；该参数仍可用于
  单独检查基准源码。
- 当前依赖审计可能报告 npm dependency vulnerabilities，公开发布前应单独评估并记录处理结论。

## 前端检查

前端工程位于 `src/web`，并包含独立的 `package-lock.json`：

```bash
cd src/web
npm ci
npm test
npm run build
npm run resource-i18n:check
```

说明：

- `npm test` 显式执行 API、请求、参数、流程及组件等行为检查；前端 CI 在构建前执行它。
- `npm run build` 的 `prebuild` 只执行 `i18n:check`，不再隐式运行行为测试。
- `i18n:check` 调用 `i18n:check-locales`，检查双语键一致性、短译键及源码使用的翻译键。
  术语文档用于人工维护，不再强制其表格与运行时配置同步。
- 弹窗按钮措辞不再由独立正则校验强制限制；组件行为和浏览器布局检查继续保留，
  可选工作流不再生成源码或 CSS 变体来验证这些检查本身。
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

- hook、CI 和手动 cppcheck 检查统一通过此脚本执行。`--profile full`（默认）启用 warning、style、performance、portability；`--profile commit` 保留提交检查的 warning 类别，两者均包含默认的 error 检查。所有已启用且未抑制的诊断、工具错误均返回失败。
- `--staged` 检查暂存的 `.h` / `.cc` 文件；`--base-ref REF` 检查 REF 与 HEAD 之间 `src` / `test` 下的变更文件，两者互斥。CI 计算 merge-base 后传入 `--base-ref`，删除文件不参与检查。
- 直接运行 `--cppcheck` 时缺少工具会失败；hook 和 `--all` 会跳过未安装的工具，`--all` 汇总已执行检查的失败。
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
# 省略型号时默认 bm1688
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

Windows PowerShell：

```powershell
# 省略型号时默认 bm1688
.\scripts\build_sophon_package.ps1
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

Sophon 发布包构建依赖交叉编译环境和 Sophon SDK。型号决定内部资源目录和芯片隔离的
输出目录；`build_output/<profile>/<chip>/` 同时包含 `TARGET_CHIP`、`SHA256SUMS` 和
`cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz`。

## Rockchip 交叉编译矩阵

`.github/workflows/ci-build-rockchip.yml` 使用共享 Rockchip Compose 入口，对
RK3576 和 RV1126B 分别运行矩阵任务。它会在手动触发及每日北京时间 02:12
（UTC 前一日 18:12）运行。定时工作流只有进入 GitHub 默认分支后才会生效。

本地使用固定 digest 的公开 GHCR 镜像，无需 registry 登录：

```bash
docker compose -f docker-compose.rockchip.yml pull cosmo-rockchip-package
COSMO_TARGET_CHIP=rk3576 docker compose -f docker-compose.rockchip.yml \
  run --rm cosmo-rockchip-package
```

工作流执行以下检查：

1. 从 `Dockerfile.rockchip` 构建同一个锁定镜像，并验证共享 Compose 配置。
2. 从干净的 `build_rknn/` 为两个芯片分别交叉编译、构建测试程序和打包。
3. 要求 `build_output/<chip>/` 中只存在一个普通文件类型的包，并校验目标标记、
   媒体 profile 与 SHA-256。
4. 确认 `cosmo-tests`、`cosmo-rknn-backend-smoke` 和
   `cosmo-rknn-fastpath-qualify` 都是 ARM aarch64 程序。
5. RK3576 包必须包含 RKLLM 运行库与许可证；RV1126B 包必须不包含它们。
6. 上传每个芯片的包、身份文件、校验和及三个验证程序，保留 7 天。

RV1126B 矩阵使用 `COSMO_PACKAGE_MODELS=include`，从已归档的 AGPL-3.0 社区示例
artifact manifest 生成 overlay，并校验两个模型、bundle 清单和许可证是否随包交付。
该矩阵只证明公开示例包可构建，不把示例模型描述为商业交付或自研模型。商业/专有模型仍需
使用独立清单，在已授权环境中构建并上板验证。
普通构建任务只有 `contents: read`；仅手动发布任务获得
`packages: write`，将通过矩阵的共享镜像推送到 GHCR。同一分支的重叠运行会取消旧任务。
GitHub 托管的 x86 runner 只交叉编译和审计，不执行 aarch64 程序。
