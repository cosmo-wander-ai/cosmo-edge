# Contributing to CosmoEdge

First off, thank you for taking the time to contribute! We welcome contributions from everyone, whether you are fixing a typo in the documentation, reporting a bug, or proposing a performance optimization for our C++ engine core.

The following guidelines are designed to help make the contribution process smooth, efficient, and aligned with our project's technical and legal standards.

---

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md). Please report any violations or inappropriate behavior to [hello@cosmowander.ai](mailto:hello@cosmowander.ai).

---

## How Can I Contribute?

### 1. Reporting Bugs
Before submitting a bug report, please search the [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) to ensure the problem hasn't already been reported.

When filing an issue, please use our bug report template and include:
*   **Environment Info**: OS version, CPU/NPU platform (e.g., Sophon BM1688, x86 CPU), SDK version (e.g., Sophon SDK v24.04).
*   **Steps to Reproduce**: A clear, step-by-step description of how to reproduce the issue.
*   **Logs**: Terminal outputs, system logs, or crash dumps (scrubbed of any sensitive customer info/IPs).
*   **Expected vs Actual Behavior**.

### 2. Suggesting Features or Enhancements
We welcome ideas for new features, pipeline nodes, or support for additional models. Please file a **Feature Request** issue to describe the use case, why this feature would be valuable, and how you envision its integration into the visual pipeline.

### 3. Submitting Pull Requests (PRs)
To keep the engine core stable and maintain high code quality, please follow this workflow for code changes:

1.  **Open an Issue First**: For any non-trivial code changes (more than 50 lines), please open an issue to discuss your proposed design before writing code. This helps ensure your work aligns with the project's roadmap and C++ architecture.
2.  **Fork and Branch**: Fork the repository and create a branch from `main`. Name your branch descriptively (e.g., `fix/rtsp-reconnect-latency` or `feat/yolov11-node`).
3.  **Keep it Focused**: Avoid mixing unrelated fixes or features in a single PR. Small, incremental PRs are much easier to review and merge quickly.
4.  **Test Your Changes**: Verify that your changes build cleanly on x86, run unit tests, and do not introduce memory leaks.

---

## Technical & Style Guidelines

### C++ Standards & Code Style
CosmoEdge is written in modern C++17.
*   **Style Guide**: We follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
*   **Formatting**: Use `clang-format` to format your code before submitting a PR. We provide a `.clang-format` file in the repository root. You can format your changes by running:
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
