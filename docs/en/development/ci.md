---
title: CI and Quality Checks
description: Entry points for documentation site, frontend, C++ formatting, static analysis, and release build quality checks for open-source collaboration.
prev:
  text: Backend Development
  link: /en/development/backend
next: false
---

# CI and Quality Checks

This page collects the quality-check entry points that already exist in the repository and can be gradually wired into CI. Before going fully public, it is recommended to put lightweight checks into GitHub Actions first, and to keep hardware-dependent or long-running checks as manual workflows or on self-hosted runners.

## Recommended Check Layers

| Layer | Check | Suggested Trigger |
| --- | --- | --- |
| Documentation site | `npm ci`, `npm run docs:build` | Pull request / push |
| Frontend | `npm ci`, `npm run i18n:check`, `npm run build`, `npm run resource-i18n:check` | Pull request / push |
| C++ formatting | `scripts/format_check.sh --check` | Pull request / push |
| C++ static analysis | `scripts/static_analysis.sh --cppcheck`, `scripts/static_analysis.sh --clang-tidy` | Periodic / manual / self-hosted |
| CPU test build | `scripts/build_cpu_test.sh`, `build_cpu/cosmo-tests` | Pull request / manual |
| x86 Docker | `docker compose -f docker-compose.x86.yml up -d --build` (use `docker-compose.x86.windows.yml` on Windows) | Manual / before release |
| Sophon release package | `scripts/build_sophon_package.sh` | Manual / self-hosted |

## Documentation Site Checks

The root `package.json` drives the VitePress documentation site:

```bash
npm ci
npm run docs:build
```

Local preview:

```bash
npm run docs:preview
```

Notes:

- The documentation build verifies VitePress pages, navigation, and in-site links.
- The repository already contains `.github/workflows/docs.yml` (a manually triggered `workflow_dispatch` workflow that builds VitePress and deploys it to GitHub Pages).
- Dependency auditing may currently report npm dependency vulnerabilities; these should be evaluated separately before public release and the resolution recorded.

## Frontend Checks

The frontend project is located under `src/web` and ships with its own independent `package-lock.json`:

```bash
cd src/web
npm ci
npm run i18n:check
npm run build
npm run resource-i18n:check
```

Notes:

- `npm run build` runs `npm run i18n:check` automatically via `prebuild`.
- `resource-i18n:check` verifies that resource-side internationalization content is in sync.
- If you modify resource text, run `npm run resource-i18n:sync` first, then review the diff.

## C++ Formatting Checks

The repository provides `scripts/format_check.sh`:

```bash
bash scripts/format_check.sh --check
```

Check only staged files:

```bash
bash scripts/format_check.sh --staged --check
```

Auto-format:

```bash
bash scripts/format_check.sh --fix
```

Notes:

- The script checks `.h` / `.cc` files under `src` and `test`.
- Requires `clang-format` to be installed locally.
- Directories such as `3rd` and `build` are excluded.

## C++ Static Analysis

The repository provides `scripts/static_analysis.sh`:

```bash
bash scripts/static_analysis.sh --cppcheck
bash scripts/static_analysis.sh --clang-tidy
bash scripts/static_analysis.sh --all
```

Notes:

- `cppcheck` is a good candidate to wire into CI first; it covers warning, style, performance, and portability categories.
- `clang-tidy` depends on `build/compile_commands.json` and requires the corresponding build configuration to be completed first.
- `--summary` aggregates common compile warnings from `build.log`.

## CPU Test Build

CPU test build script:

```bash
bash scripts/build_cpu_test.sh
```

The script configures `build_cpu`, enables `BUILD_TESTS=ON`, and builds:

```text
build_cpu/cosmo-tests
```

After the build completes you can run:

```bash
./build_cpu/cosmo-tests
```

Notes:

- This path uses the x86 CPU backend and ONNX Runtime.
- The script generates or links `compile_commands.json` for IDE and static-analysis tooling.
- The script currently reports that `pkg-config` and the OpenH264 development package are required.

## x86 Docker Validation

The x86 development mode can be used for integration-level validation:

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

Before a release, confirm at minimum:

- The web console is reachable.
- Core service processes start normally.
- Common ports are not in conflict.
- The first-run experience path is not blocked.

## Sophon Release Package Validation

Sophon/aarch64 release package build entry point:

```bash
bash scripts/build_sophon_package.sh
```

Windows PowerShell:

```powershell
.\scripts\build_sophon_package.ps1
```

The Sophon release package build depends on the cross-compilation environment and the Sophon SDK. The package exported into `build_output/` is named in the form `cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz`.
