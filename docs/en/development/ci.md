---
title: CI and Quality Checks
description: Recommended quality gates for docs, frontend, C++ formatting, static analysis, tests, and release validation.
prev:
  text: Backend Development
  link: /en/development/backend
next: false
---

# CI and Quality Checks

This page describes practical CI layers for an open-source repository. Some checks may initially run manually or on self-hosted runners until public infrastructure is ready.

## Recommended Quality Gates

| Layer | Command / Entry | Notes |
| --- | --- | --- |
| Documentation | `npm ci`, `npm run docs:build` | Verifies VitePress pages and links. |
| Frontend | frontend package scripts | Confirm exact command under `src/web/`. |
| C++ formatting | `scripts/format_check.sh --check` | Requires `clang-format`. |
| C++ static analysis | `scripts/static_analysis.sh --cppcheck`, `scripts/static_analysis.sh --clang-tidy` | `clang-tidy` needs `compile_commands.json`. |
| CPU build/tests | CPU build scripts and test target | Good first public CI candidate. |
| Release package | Sophon package script | May require self-hosted runner and SDK. |

## Documentation

```bash
npm ci
npm run docs:build
```

On Windows PowerShell, use `npm.cmd run docs:build` if script execution policy blocks `npm.ps1`.
