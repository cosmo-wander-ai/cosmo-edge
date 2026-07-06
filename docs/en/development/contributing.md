---
title: Contributor Guide
description: First-time contributor workflow, local setup, validation commands, and pull request checklist.
prev:
  text: Models and Resources
  link: /en/reference/models
next:
  text: Frontend Development
  link: /en/development/frontend
---

# Contributor Guide

This page is for first-time CosmoEdge contributors. Its goal is to make the first local loop clear: where to start, what to run, and what to put in a pull request. The root [CONTRIBUTING.md](https://github.com/cosmo-wander-ai/cosmo-edge/blob/main/CONTRIBUTING.md) remains the source of truth for project rules.

## Good First Contribution Areas

| Area | Suggested scope | Main validation |
| --- | --- | --- |
| Documentation | Typos, links, tutorial notes, command fixes | `npm run docs:build` |
| Frontend polish | Text, i18n, form validation, page interaction fixes | `npm run build` |
| Focused C++ fix | Utility functions, DTOs, service-layer fixes, unit tests | `scripts/build_cpu_test.sh` and `cosmo-tests` |
| Scenario or model notes | Example config, parameter explanations, integration notes | Docs build + related manual check |

For broad C++ architecture changes, new algorithm nodes, new dependencies, or model-runtime integrations, please open an issue first and discuss the design.

## Local Setup

Recommended tools:

- Git and a GitHub account.
- Node.js / npm for the docs site and frontend builds.
- Docker Desktop or Docker Engine with Docker Compose V2.
- For C++ backend work: Bash, CMake, a C++ compiler, `pkg-config`, `clang-format`, and any system packages reported by `scripts/build_cpu_test.sh`.
- Optional: `cppcheck` for local static analysis.

Windows contributors can start with the Docker and PowerShell paths. The native C++ test scripts are written for a Bash environment.

## Recommended Workflow

1. Fork the repository and create a descriptive branch from `main`, such as `docs/contributor-guide` or `fix/auth-token-refresh`.
2. Keep the first change small enough to review comfortably.
3. Follow the existing directory layout, naming style, and test style.
4. Before opening a PR, run the smallest validation set that matches your change.
5. In the PR description, explain what changed, why it changed, and which checks you ran.
6. Use `git commit -s` so commits include the DCO sign-off.

## Validation Command Reference

### Documentation

```bash
npm ci
npm run docs:build
```

Local preview:

```bash
npm run docs:preview
```

### Frontend

```bash
cd src/web
npm ci
npm run i18n:check
npm run build
npm run resource-i18n:check
```

For ordinary page logic changes, `npm run build` automatically runs `i18n:check` first. If you changed resource-side i18n content, also run `resource-i18n:check`.

### C++ Backend

Check staged C++ formatting:

```bash
bash scripts/format_check.sh --staged --check
```

Auto-format staged C++ files:

```bash
bash scripts/format_check.sh --staged --fix
git add -u
```

Build and run the CPU-backend tests:

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests
```

Optional static analysis:

```bash
bash scripts/static_analysis.sh --cppcheck --staged
```

### x86 Docker Runtime Smoke Test

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

After startup, the web console is available at `http://127.0.0.1:8080`.

### Git Pre-commit Hook

CosmoEdge includes a Git pre-commit hook that automatically checks staged C++ files for formatting issues (via `clang-format`) and runs `cppcheck` static analysis if available. Installing the hook catches problems before they reach CI.

**Install the hook:**

```bash
bash scripts/install-hooks.sh
```

This creates a symlink from `.git/hooks/pre-commit` to `scripts/pre-commit`, so the hook stays in sync with the repository.

**What the hook checks:**

1. **clang-format** — all staged `.h` and `.cc` files must follow the project style (`.clang-format` at repo root).
   - If formatting fails: run `bash scripts/format_check.sh --staged --fix`, then `git add -u`.
2. **cppcheck** (optional) — runs only if `cppcheck` is installed locally. Blocks on error-level findings.

**Uninstall the hook:**

```bash
rm .git/hooks/pre-commit
```

**Manual checks (without the hook):**

```bash
# Check formatting of staged files
bash scripts/format_check.sh --staged --check

# Auto-fix formatting of staged files
bash scripts/format_check.sh --staged --fix
git add -u

# Run cppcheck on staged files (if installed)
bash scripts/static_analysis.sh --cppcheck --staged
```

## Pull Request Checklist

- The change is focused on one topic.
- Related docs, examples, or tests were updated.
- You ran the validation commands relevant to the change.
- The PR template's **Verification** section lists the actual commands you ran.
- No secrets, customer details, private IPs, private model weights, or proprietary download links are included.
- Any new third-party dependency, model, dataset, or asset has a documented source and license.
- Commits include `Signed-off-by:`.

## Common Stumbling Blocks

| Problem | Suggested action |
| --- | --- |
| Unsure whether to run all checks or only a subset | Use the validation reference and pick the smallest relevant set. |
| No Sophon device available | Docs, frontend, x86 Docker, and CPU test builds do not require Sophon hardware. |
| `docker compose` is unavailable | Install Docker Compose V2, or replace commands with `docker-compose` in older environments. |
| C++ dependencies are missing | Run `bash scripts/build_cpu_test.sh` first and install the packages reported by the script. |
| Formatting check fails | Run `bash scripts/format_check.sh --staged --fix`, then stage the updated files again. |

If a change is larger than 50 lines or affects public APIs, deployment scripts, model formats, or pipeline semantics, open an issue first to describe the design.
