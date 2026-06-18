---
title: Backend Development
description: Backend build entry points, CMake options, tests, style, and service development notes.
prev:
  text: Frontend Development
  link: /en/development/frontend
next:
  text: CI and Quality Checks
  link: /en/development/ci
---

# Backend Development

The backend is a native C++ application that provides runtime services, media processing, inference integration, APIs, and event output.

## Build Entry Points

Use the documented build scripts rather than ad-hoc commands when validating changes:

- `scripts/build_cpu_windows.ps1` for Windows CPU validation;
- `scripts/build_sophon_package.sh` for target release package creation;
- CMake configuration for lower-level development workflows.

## CMake Options

Important options include CPU backend selection, ONNX Runtime integration, Sophon-related runtime integration, and optional model guard linkage. Confirm release defaults before documenting them as stable.

## Tests

Run the available test target after configuring the matching build directory. If tests require runtime resources or hardware, document the requirement in the test plan.

## Code Style

Follow existing C++ formatting and naming conventions. Prefer small service-oriented changes that keep API, media, flow, and model boundaries clear.
