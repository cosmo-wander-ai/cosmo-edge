# Contributing to CosmoEdge

First off, thank you for taking the time to contribute! We welcome contributions from everyone, whether you are fixing a typo in the documentation, reporting a bug, or proposing a performance optimization for our C++ engine core.

The following guidelines are designed to help make the contribution process smooth, efficient, and aligned with our project's technical and legal standards.

---

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md). Please report any violations or inappropriate behavior to [hello@cosmowander.ai](mailto:hello@cosmowander.ai).

---

## First-Time Contributor Path

If you are new to the project, start with the smallest path that matches your change. You do not need Sophon hardware for documentation fixes, frontend work, or most x86 CPU-backend C++ validation.

| Change type | Good first validation |
| --- | --- |
| Documentation only | `npm ci` and `npm run docs:build` |
| Frontend web console | `cd src/web`, `npm ci`, `npm run build` |
| C++ backend or tests | `bash scripts/format_check.sh --staged --check`, `bash scripts/build_cpu_test.sh`, `./build_cpu/cosmo-tests` |
| Runtime smoke test | `docker compose -f docker-compose.x86.yml up -d --build` on Linux, or `docker compose -f docker-compose.x86.windows.yml up -d --build` on Windows |
| Sophon release package | `./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package [--chip <model>]`, or `.\scripts\build_sophon_package.ps1 [-Chip <model>]` on Windows; models are `bm1688` / `cv186x`, defaulting to `bm1688` |

For a fuller contributor-oriented walkthrough, see:

- [Contributor Guide](docs/en/development/contributing.md)
- [贡献者上手路径](docs/development/contributing.md)
- [Build Guide](docs/en/guide/build.md)
- [CI and Quality Checks](docs/en/development/ci.md)

### Suggested Local Workflow

1. Fork the repository and create a branch from `main`.
2. Keep the change focused on one bug, document, feature, or refactor.
3. Run the smallest relevant validation commands from the table above.
4. Include the commands you ran in the pull request's **Verification** section.
5. Sign off your commits with `git commit -s`.

### Useful Local Tools

- `clang-format` is required for C++ formatting checks.
- `cppcheck` is optional locally, but useful before touching shared C++ modules.
- Docker Compose V2 (`docker compose`) is the documented path. If you use Docker Compose V1, replace `docker compose` with `docker-compose`.
- Native CPU test builds require the normal C++ build toolchain plus packages reported by `scripts/build_cpu_test.sh` if they are missing.

---

## How Can I Contribute?

### 1. Reporting Bugs
Before submitting a bug report, please search the [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) (or [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) for China users) to ensure the problem hasn't already been reported.

When filing an issue, please use our bug report template and include:
*   **Environment Info**: OS version, CPU/NPU platform (e.g., Sophon BM1688, x86 CPU), SDK version (e.g., Sophon SDK v24.04).
*   **Steps to Reproduce**: A clear, step-by-step description of how to reproduce the issue.
*   **Logs**: Terminal outputs, system logs, or crash dumps (scrubbed of any sensitive customer info/IPs).
*   **Expected vs Actual Behavior**.

### 2. Suggesting Features or Enhancements
We welcome ideas for new features, pipeline nodes, or support for additional models. Please file a **Feature Request** issue to describe the use case, why this feature would be valuable, and how you envision its integration into the visual pipeline.

### 3. Submitting Pull Requests (PRs)
To keep the engine core stable and maintain high code quality, please follow this workflow for code changes:

1.  **Describe the Change**: Explain the reason and scope in your PR. For broad architecture changes or new dependencies, an issue can help discuss the design, but it is optional; line count does not require one.
2.  **Fork and Branch**: Fork the repository and create a branch from `main`. Name your branch descriptively (e.g., `fix/rtsp-reconnect-latency` or `feat/yolov11-node`).
3.  **Keep it Focused**: Avoid mixing unrelated fixes or features in a single PR. Small, incremental PRs are much easier to review and merge quickly.
4.  **Test Your Changes**: Verify that your changes build cleanly on x86, run relevant unit tests, and do not introduce memory leaks or lifecycle issues.

---

## Technical & Style Guidelines

### C++ Standards & Code Style
CosmoEdge is written in modern C++17.
*   **Style Guide**: We follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
*   **Formatting**: Use `clang-format` to format your code before submitting a PR. We provide a `.clang-format` file in the repository root. For most changes, prefer the repository helper:
    ```bash
    bash scripts/format_check.sh --staged --check
    bash scripts/format_check.sh --staged --fix
    ```
    You can also format an individual file by running:
    ```bash
    clang-format -i path/to/modified_file.cc
    ```
*   **Safety**: Write clean, modern C++ (RAII, smart pointers, avoid raw `new`/`delete`, minimize singletons, and leverage our Dependency Injection container via `ServiceRegistry`).

### Commit Message Convention
We use the **Conventional Commits** specification. This helps us generate clean changelogs automatically. Your commit message should look like this:

```text
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

**Common Types**:
*   `feat`: A new feature (e.g., a new pipeline analysis node).
*   `fix`: A bug fix.
*   `docs`: Documentation changes.
*   `style`: Code style/formatting changes (no logic changes).
*   `refactor`: Code changes that neither fix a bug nor add a feature.
*   `perf`: A code change that improves performance.
*   `test`: Adding missing tests or correcting existing tests.

*Example*: `feat(flow): add directional crossline counting node`

---

## Legal & Licensing Compliance

### 1. License Agreement
By contributing to CosmoEdge, you agree that your contributions will be licensed under the project's **Apache License 2.0**.

### 2. Developer Certificate of Origin (DCO)
To maintain clear ownership and copyright tracking, all commits must be signed off by the author, certifying that you have the right to submit the code under the open-source license.

To sign off your commit, add `-s` or `--signoff` when committing:
```bash
git commit -s -m "feat(flow): add directional crossline counting node"
```
This appends a line to your commit message:
`Signed-off-by: Your Name <your.email@example.com>`

### 3. ⚠️ IMPORTANT: GPL/LGPL Contamination Warning
CosmoEdge is designed to be commercially friendly. We strictly prohibit the introduction of dependencies or copy-pasted code licensed under **GPL** (General Public License) or similar strong copyleft licenses.
*   **No GPL Code**: Do not submit code that incorporates GPL-licensed code.
*   **FFmpeg Configuration**: When working on the video decoding and encoding modules, maintain strict isolation. Keep the FFmpeg dependency at LGPL v2.1. Do not submit configurations that enable GPL encoders (such as `libx264` under CPU backend configurations) for production distribution. Use permissive alternatives (such as OpenH264 or native Sophon hardware acceleration codecs).
*   **Third-party Libraries**: All new dependencies must be approved first and must use permissive licenses (such as Apache 2.0, MIT, or BSD).

---

Thank you again for contributing to CosmoEdge!
