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
| Frontend | `npm ci`, `npm run i18n:check`, `npm run build`, `npm run resource-i18n:check` | Pull request / push |
| C++ formatting | `scripts/format_check.sh --check` | Pull request / push |
| C++ static analysis | `scripts/static_analysis.sh --cppcheck`, `scripts/static_analysis.sh --clang-tidy` | Periodic / manual / self-hosted |
| CPU test build | `scripts/build_cpu_test.sh`, `build_cpu/cosmo-tests` | Pull request / manual |
| x86 Docker | `docker compose -f docker-compose.x86.yml up -d --build` (use `docker-compose.x86.windows.yml` on Windows) | Manual / before release |
| Sophon release package | `./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package [--chip <model>]`; supports `bm1688` / `cv186x` (defaults to `bm1688`) | Manual / self-hosted |
| Rockchip release package | `COSMO_TARGET_CHIP=<rk3576|rv1126b> docker compose -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package` | Daily at 02:12 Beijing Time / manual |

## Documentation Site Checks

The root `package.json` drives the VitePress documentation site:

```bash
npm ci
npm run docs:verify
```

`docs:verify` runs:

1. `docs:check`: validates frontmatter, one H1, placeholders, image alt text, internal links, bilingual
   pairs, and navigation groups for the five bilingual core guides and two indexes.
2. `docs:build`: builds the complete VitePress site and checks VitePress parsing and internal links.
3. `docs:smoke`: inspects rendered HTML for the ten core pages, including titles, locale, navigation,
   groups, and frontmatter leakage.

Local preview:

```bash
npm run docs:preview
```

Notes:

- The `docs` job in `.github/workflows/pr-checks.yml` runs the same `npm run docs:verify` command on pull
  requests.
- Placeholder checks use an explicit core-guide manifest, so intentional editing prompts in community
  case templates are not rejected.
- The VitePress build checks the full site, navigation, and internal links. The ten-page semantic smoke
  adds detection for frontmatter leaking into content even when the build succeeds.
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
# Defaults to bm1688 when the chip model is omitted
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

Windows PowerShell:

```powershell
# Defaults to bm1688 when the chip model is omitted
.\scripts\build_sophon_package.ps1
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

The Sophon release package build depends on the cross-compilation environment and
the Sophon SDK. The chip model selects both the internal resource directory and
the chip-scoped output. Each `build_output/<profile>/<chip>/` directory contains
`TARGET_CHIP`, `SHA256SUMS`, and one
`cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz` archive.

## Rockchip Cross-Build Matrix

`.github/workflows/ci-build-rockchip.yml` uses the shared Rockchip Compose entry
for separate RK3576 and RV1126B matrix jobs. It runs on manual dispatches and
every day at 02:12 Beijing Time (18:12 UTC on the previous day). A schedule is
active only on the GitHub default branch.

Local builds use the public digest-pinned GHCR image without registry login:

```bash
docker compose -f docker-compose.rockchip.yml pull cosmo-rockchip-package
COSMO_TARGET_CHIP=rk3576 docker compose -f docker-compose.rockchip.yml \
  run --rm cosmo-rockchip-package
```

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
