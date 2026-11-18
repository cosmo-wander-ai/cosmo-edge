---
title: CI and Quality Checks
description: Entry points for documentation, frontend, C++, static analysis, and platform release-build checks.
prev:
  text: Backend Development
  link: /en/development/backend
next:
  text: Agent-Assisted Development
  link: /en/development/agent-assisted-development
---

# CI and Quality Checks

This page collects the quality-check entry points that already exist in the repository and can be gradually wired into CI. Before going fully public, it is recommended to put lightweight checks into GitHub Actions first, and to keep hardware-dependent or long-running checks as manual workflows or on self-hosted runners.

## Recommended Check Layers

| Layer | Check | Suggested Trigger |
| --- | --- | --- |
| Documentation site | `npm ci`, `npm run docs:verify` | Pull request |
| Frontend | `npm ci`, `npm test`, `npm run build`, `npm run resource-i18n:check` | Pull request / push |
| C++ formatting | `scripts/format_check.sh --check` | Pull request / push |
| C++ static analysis | `scripts/static_analysis.sh --cppcheck`, `scripts/static_analysis.sh --clang-tidy` | Periodic / manual / self-hosted |
| CPU test build | `scripts/build_cpu_test.sh`, `build_cpu/cosmo-tests` | Pull request / manual |
| x86 Docker | [Build and start for the host environment](../guide/build.md#x86-docker-development-runtime) | Manual / before release |
| Sophon release package | [Select BM1688 / CV186X and the host entry](../guide/build.md#sophon-artifacts) | Manual / self-hosted |
| Rockchip release package | [Select RK3576 / RV1126B](../guide/build.md#rockchip-artifacts) | Daily at 02:12 Beijing Time / manual |

## Documentation Site Checks

The root `package.json` drives the VitePress documentation site:

```bash
npm ci
npm run docs:verify
```

`docs:verify` runs:

1. `gitee:readme:check`: checks that the Gitee README matches its sources.
2. `docs:build`: builds the complete VitePress site, checks internal links, and generates benchmark reports.
3. `benchmarks:v1.1:validate -- --output-root docs/.vitepress/dist/benchmarks/scenario-bench/v1.1`:
   directly validates benchmark sources and the reports from this build, including evidence,
   checksums, public-data boundaries, and links within the benchmark pack.

Local preview:

```bash
npm run docs:preview
```

Notes:

- The `docs` job in `.github/workflows/pr-checks.yml` runs the same `npm run docs:verify` command on pull
  requests.
- VitePress compiles tutorials and checks internal links. Dedicated scripts no longer constrain
  fixed pages' titles, H1 headings, navigation, image alt text, or frontmatter rendering.
- Full benchmark validation already checks sources, so `--source-only` is not repeated before the build.
  The option remains available for checking benchmark sources independently.
- Benchmark validation retains report completeness, data projections, evidence scope, checksums, and
  links within the pack without prescribing CSS classes or counts, titles, or explanatory wording.
- Dedicated scanning of website report URLs in READMEs and other documentation has been removed,
  including mapping those absolute URLs to local build targets. Report inventories and relative links
  within the benchmark pack are still validated.
- Dependency auditing may currently report npm dependency vulnerabilities; these should be evaluated separately before public release and the resolution recorded.

## Frontend Checks

The frontend project is located under `src/web` and ships with its own independent `package-lock.json`:

```bash
cd src/web
npm ci
npm test
npm run build
npm run resource-i18n:check
```

Notes:

- `npm test` uses Node's native test runner to run explicitly listed test entrypoints serially,
  covering API, request, parameter, flow, and component behavior. Frontend CI runs it before building.
  Run an individual file with `node --experimental-vm-modules --test scripts/<test-file>.mjs`.
- The `prebuild` hook for `npm run build` runs only `i18n:check`, without implicitly running behavior tests.
- `i18n:check` directly checks bilingual key parity, short keys, and translation keys used in source files.
  Glossary documentation is maintained manually; its tables are no longer required to match runtime configuration.
- Dialog button wording is no longer restricted by a separate regex check. Component behavior and browser
  layout checks remain; the optional workflow no longer generates source or CSS variants to test those checks.
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

- The hook, CI, and manual cppcheck checks all use this script. `--profile full` (the default) enables warning, style, performance, and portability; `--profile commit` retains the commit check's warning category. Both include the default error checks. All enabled, unsuppressed diagnostics and tool errors return failure.
- `--staged` checks staged `.h` / `.cc` files; `--base-ref REF` checks changed files under `src` / `test` between REF and HEAD. These options are mutually exclusive. CI passes the computed merge-base to `--base-ref`; deleted files are excluded.
- Direct `--cppcheck` invocation fails if the tool is missing. The hook and `--all` skip unavailable tools; `--all` aggregates failures from the checks it runs.
- `clang-tidy` depends on `build/compile_commands.json` and requires the corresponding build configuration to be completed first.
- `--summary` aggregates common compile warnings from `build.log`.

## CPU Test Build

Follow the [CPU test path](../guide/build.md#cpu-test-build) on a Linux x86_64 host using the
repository build container. Build `build_cpu/cosmo-tests`, then execute it separately and inspect
the result. This entry uses the CPU backend and ONNX Runtime and creates or links
`compile_commands.json`. Producing a binary is not a passing test result; builder and runtime
library settings are maintained in the Build Guide.

## x86 Docker Validation

The x86 development mode can be used for integration-level validation:

- **Linux**:
  ```bash
  ./scripts/docker-compose.sh -f docker-compose.x86.yml up -d --build
  ./scripts/docker-compose.sh -f docker-compose.x86.yml logs -f
  ./scripts/docker-compose.sh -f docker-compose.x86.yml down
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

Select BM1688 / CV186X and the host environment in the [Build Guide](../guide/build.md#sophon-artifacts).
Each `build_output/<profile>/<chip>/` directory must contain one package, `TARGET_CHIP`, and
`SHA256SUMS`. The build produces an aarch64 test binary; executing it requires a matching
device and runtime libraries. Package auditing does not replace device acceptance.

## Rockchip Cross-Build Matrix

`.github/workflows/ci-build-rockchip.yml` uses the shared Rockchip Compose entry
for separate RK3576 and RV1126B matrix jobs. It runs on manual dispatches and
every day at 02:12 Beijing Time (18:12 UTC on the previous day). A schedule is
active only on the GitHub default branch.

For a local build, select one chip through the [Rockchip Build Guide](../guide/build.md#rockchip-artifacts).
Run in Bash on a Linux x86_64 host, then verify the selected target's package and checksums.

The workflow applies these checks:

1. Builds one locked image from `Dockerfile.rockchip` and validates the shared
   Compose entry.
2. Cross-compiles, builds validation programs, and packages each chip from a
   clean `build_rknn/` directory.
3. Requires exactly one regular archive under `build_output/<chip>/` and checks
   its target marker, media profile, and SHA-256.
4. Confirms that `cosmo-tests`, `cosmo-rknn-backend-smoke`, and
   `cosmo-rknn-fastpath-qualify` are ARM aarch64 programs.
5. Requires RKLLM and its license in RK3576 and forbids them in RV1126B.
6. Uploads each package, identity files, checksum, and validation programs for
   7 days.

The RV1126B matrix uses `COSMO_PACKAGE_MODELS=include` and generates its overlay
from the archived AGPL-3.0 community example artifact manifest. It verifies
that both models, the bundle manifest, and the license reach the package. This
proves only that the public example package is buildable; it does not describe
those examples as commercial deliverables or proprietary models. Commercial or
proprietary models still require an independent manifest, an authorized build,
and board validation. Normal jobs have
only `contents: read`; only a manually dispatched publication job receives
`packages: write` and pushes a matrix-qualified shared image to GHCR. The hosted
x86 runner does not execute aarch64 programs.
