---
title: Build Guide
description: Confirmed build paths for x86 Docker, Sophon, Rockchip, CPU tests, and docs.
prev:
  text: Documentation Home
  link: /en/
next:
  text: RK3576 / RKNN Integration
  link: /en/guide/rk3576-rknn-development
---

# Build Guide

Choose the **host environment** (where you run the commands), then the **target platform**
(where the output runs). This guide describes the current repository entry points.
Run only the command group for your host and target.

## Build Path Overview

| Purpose | Host environment | Target platform | Entry point and result |
| --- | --- | --- | --- |
| Local trial | Linux x86_64 | x86 CPU | [Linux Compose](#linux-x86-64); starts services; `build_output/` |
| Local trial | Windows x86_64 / Docker Desktop | x86 CPU | [Windows Compose](#windows-x86-64); starts services; `build_output/` |
| Local trial | Apple Silicon macOS | amd64 emulation | [Mac Preview](#apple-silicon-macos-preview); starts services; `build_output/macos-x86/` |
| Installation package | Linux x86_64 or Windows x86_64 / PowerShell | BM1688 | [Sophon BM1688](#bm1688); `build_output/public-runtime/bm1688/` |
| Installation package | Linux x86_64 or Windows x86_64 / PowerShell | CV186X | [Sophon CV186X](#cv186x); `build_output/public-runtime/cv186x/` |
| Installation package | Linux x86_64 / Bash | RK3576 | [Rockchip RK3576](#rk3576); `build_output/rk3576/` |
| Installation package | Linux x86_64 / Bash | RV1126B | [Rockchip RV1126B](#rv1126b); `build_output/rv1126b/` |
| C++ unit tests | Linux x86_64 / build container | x86 CPU | [CPU tests](#cpu-test-build); `build_cpu/cosmo-tests` |

## Common Preparation

The host needs Git, working Docker, and the appropriate Compose entry point. Use Docker
Desktop's Linux container mode on Windows. Mac users should first read the
[Preview requirements](./macos-docker-preview.md). Initial builds need access to image
registries and dependency sources. Cross-compilation runs in a build container on the host,
not on the target device.

For a new checkout, use Bash or PowerShell:

```sh
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
```

For an existing checkout, enter its root instead of cloning again. All host commands below
run from the repository root. The default is current `main`; to build a stable release,
select its tag separately before building, for example:

```sh
git checkout v1.2.0
```

Use the documentation and scripts from that tag. Package versions depend on the source and
Git state. Install the actual filename reported by the build and `SHA256SUMS`, not an old
version copied from a tutorial.

Linux Compose commands use `./scripts/docker-compose.sh`, which selects V2/V1 and requests
sudo when the account cannot access the daemon. Windows commands use PowerShell; the Sophon
PowerShell script and Mac Preview require Compose V2. Ordinary builds below assume the
Open (`public-runtime`) and `COSMO_PACKAGE_MODELS=include` defaults. Restore those defaults
if you previously set overrides. See [advanced options](#advanced-build-options) for other profiles.

## x86 Docker Development Runtime

Use this ONNX Runtime CPU path for local evaluation and development. Select one of the
following three host environments.

### Linux x86_64

Run in Bash on the Linux host:

```bash
./scripts/docker-compose.sh -f docker-compose.x86.yml up -d --build
./scripts/docker-compose.sh -f docker-compose.x86.yml ps
```

### Windows x86_64

Start Docker Desktop, then run in host PowerShell:

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
```

### Apple Silicon macOS Preview

Run admission in the Mac terminal and start only after it succeeds:

```bash
./scripts/macos-docker-preview.sh doctor && ./scripts/macos-docker-preview.sh up
./scripts/macos-docker-preview.sh status
```

Mac uses `linux/amd64` emulation and isolated volumes, with loopback-only ports. After source
changes, explicitly run `./scripts/macos-docker-preview.sh up --build`. See
[macOS Docker Preview](./macos-docker-preview.md) for the complete scope.

### Startup Result and Data

Confirm the container is running and `http://127.0.0.1:8080` opens in the browser. Linux/Windows
export packages to `build_output/`; Mac exports to `build_output/macos-x86/`. Runtime data
and model resources use separate named volumes. See the
[Deployment Guide](./deployment.md#x86-docker-runtime) for stopping, updating, and retaining data.

`Dockerfile.x86` calls `scripts/build_cpu.sh` with these main settings:

| Parameter | Value |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

This path does not enable Model Guard. Local CPU and Mac emulation results do not establish target NPU performance.

## Sophon Artifacts

Build an Open installation package for a device with its Sophon Linux base system and runtime
dependencies already prepared. Select the chip, then the host. The script selects the matching
model resource directory; users do not supply a model path. These commands build and export
artifacts without starting device services.

### BM1688

**Linux x86_64 / Bash:**

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip bm1688
```

**Windows x86_64 / PowerShell:**

```powershell
.\scripts\build_sophon_package.ps1 -Chip bm1688
```

Output: `build_output/public-runtime/bm1688/`.

### CV186X

**Linux x86_64 / Bash:**

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip cv186x
```

**Windows x86_64 / PowerShell:**

```powershell
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

Output: `build_output/public-runtime/cv186x/`. See [CV186X Quick Start](./cv186x-quick-start.md)
for device identity and the first detection. The script defaults to BM1688 when the chip is
omitted; the examples above always select it explicitly.

### Hand a Sophon Build to the Deployment Workflow

A successful output directory contains exactly one `cosmo-V<version>-<32-char-md5>.tar.gz`,
`TARGET_CHIP`, and `SHA256SUMS`. The archive also contains `share/cosmo/target-chip.txt`.
Match the chip marker to the actual device. Matching bytes in some models do not make
complete packages interchangeable between chips.

**Linux / Bash verification** (BM1688 example; change `chip` to `cv186x` for CV186X):

```bash
chip=bm1688
(cat "build_output/public-runtime/${chip}/TARGET_CHIP" &&
 cd "build_output/public-runtime/${chip}" && sha256sum -c SHA256SUMS)
```

**Windows / PowerShell verification** (select the same single chip):

```powershell
$chip = 'bm1688'
$outputDir = Join-Path 'build_output/public-runtime' $chip
if ((Get-Content -LiteralPath (Join-Path $outputDir 'TARGET_CHIP') -Raw).Trim() -ne $chip) {
    throw 'Target chip mismatch'
}
$checksumLines = @(Get-Content -LiteralPath (Join-Path $outputDir 'SHA256SUMS'))
if ($checksumLines.Count -ne 1 -or $checksumLines[0] -notmatch '^([0-9a-fA-F]{64})\s+\*?(cosmo-V[0-9]+\.[0-9]+\.[0-9]+-[0-9a-fA-F]{32}\.tar\.gz)$') {
    throw 'Expected one package checksum'
}
$expectedHash = $Matches[1]
$packageName = $Matches[2]
$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $outputDir $packageName)).Hash
if ($actualHash -ne $expectedHash) { throw 'SHA-256 mismatch' }
Write-Output "Verified $chip package: $packageName"
```

After verification, use that actual filename in the
[SSH installation path](./deployment.md#ssh-installation-path) or web upgrade flow.
The build also generates the aarch64 `build/cosmo-tests` binary but does not execute it.
Passing tests and device business acceptance each require their own execution evidence.

## Rockchip Artifacts

Run these commands in **Bash at the repository root on a Linux x86_64 host** to build an
aarch64 Open package for RK3576 or RV1126B. This section does not provide a Windows or
macOS cross-compilation entry point.

First pull the shared builder pinned by digest in the repository. Do this once for the image in use:

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package
```

Then choose one chip below.

### RK3576

```bash
COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

After success, verify on the host that the marker is `rk3576` and the SHA-256 check passes:

```bash
cat build_output/rk3576/TARGET_CHIP
(cd build_output/rk3576 && sha256sum -c SHA256SUMS)
```

Output: `build_output/rk3576/`. For Protected, see [RK3576 Protected Build](#rk3576-protected-build). RKLLM Runtime v1.3.0 and its license are mandatory in this
package. Qwen3.5 models are not distributed with the Open package; see
[RK3576 / RKNN Integration](./rk3576-rknn-development.md) for model and runtime contracts.

### RV1126B

```bash
COSMO_TARGET_CHIP=rv1126b ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

After success, verify on the host that the marker is `rv1126b` and the SHA-256 check passes:

```bash
cat build_output/rv1126b/TARGET_CHIP
(cd build_output/rv1126b && sha256sum -c SHA256SUMS)
```

Output: `build_output/rv1126b/`. The default `include` route generates and verifies the model
overlay automatically from the platform manifest; no manual overlay preparation is needed.
The default manifest's two AGPL-3.0 community example models are for public examples and CI,
not commercial model delivery. The package retains `resource/model-bundle.json` and the
licenses. It does not contain RKLLM.

### Artifacts and Installation

Each chip directory contains exactly one package, `TARGET_CHIP`, `MEDIA_RUNTIME_PROFILE`,
and `SHA256SUMS`. After verification, use the
[shared SSH installation entry](./deployment.md#ssh-installation-path) with the package from
your selected chip directory. Rockchip defaults to `/userdata/cwaiuserdata` for persistent
data; the base system and runtime dependencies must already be prepared.

The builder cleans `build_rknn/` in its `linux/amd64` container and calls
`scripts/build_rknn.sh -c <chip> -T`. It also generates three aarch64 programs:
`build_rknn/cosmo-tests`, `cosmo-rknn-backend-smoke`, and `cosmo-rknn-fastpath-qualify`.
It does not execute them or start the application. Build and package audits do not establish device acceptance.

## CPU Test Build

Use a **Linux x86_64 host**. From the repository root, run the same build image as CPU CI.
It provides CMake, compilers, Rust, pkg-config, OpenH264, and other build dependencies.
CPU output uses `build_cpu/`, separate from the aarch64 `build/` directory.

### Build the Test Binary

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/workspace \
  -e RUSTUP_HOME=/root/.rustup -e CARGO_HOME=/root/.cargo \
  ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_x86:v1 \
  bash -c 'bash scripts/build_cpu_test.sh'
```

Success produces `build_cpu/cosmo-tests`. The script enables the CPU backend and
`BUILD_TESTS=ON`; it does not run tests or create an installation package.

### Run the Tests

After a successful build, run from the same repository root. These library paths match CPU CI:

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_x86:v1 \
  bash -c '
    export LD_LIBRARY_PATH="\
/workspace/prebuild/ffmpeg/x86_64/lib:\
/workspace/3rd/onnxruntime-linux-x64-1.26.0/lib:\
/workspace/build_cpu/thirdparty_install/openssl/lib:\
/workspace/build_cpu/thirdparty_install/curl/lib:\
/workspace/build_cpu/thirdparty_install/event/lib:\
/workspace/build_cpu/thirdparty_install/glog/lib:\
/workspace/build_cpu/thirdparty_install/mp4v2/lib:\
/workspace/build_cpu/thirdparty_install/uuid/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    ./build_cpu/cosmo-tests
  '
```

Use the test summary and exit code to determine success. Passing CPU tests does not qualify
Sophon or Rockchip device behavior.

## Advanced Build Options

### Open and Protected by Platform

| Target platform | Open (`public-runtime`) | Protected (`production-release`) |
| --- | --- | --- |
| BM1688 | Supported | Supported with the Sophon Guard SDK |
| CV186X | Supported | Supported with the Sophon Guard SDK |
| RK3576 | Supported | Supported with the RKNN Guard SDK |
| RV1126B | Supported | Unsupported; rejected by configuration and package auditing |
| x86 / macOS Preview | CPU development mode | Model Guard unsupported |

Open uses plaintext models without device authorization or provisioning tools. Protected uses
the target platform's Guard runtime and `cosmo-model-provision`; protected models require
device authorization. All three supported Protected targets export to
`build_output/production-release/<chip>/`. Open directories are listed in the platform sections above.

Both profiles use `cosmo-V<version>-<32-char-md5>.tar.gz` and the same upgrade lifecycle.
Application archives are not signed. A Protected CPack archive can be uploaded directly
through the management page without an offline application-signing step. Models in a package
must match its profile; the SDK does not automatically encrypt the default plaintext models.

### Sophon Protected Build

The authorized fork delivers the complete Sophon Guard SDK in `prebuild/model-guard-v2/`,
including runtime, headers, matching provisioning tool, and `SDK-MANIFEST.json`. Both profiles
use it by default. Only an explicit `COSMO_MODEL_GUARD_SDK_ROOT` overrides it, and that path
must be accessible inside the container. `build_output/model-guard-sdk-production/` is no
longer selected automatically.

Example for a **CV186X Protected application update** in Linux Bash, without model weights:

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
COSMO_PACKAGE_MODELS=preserve \
  ./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip cv186x
```

For BM1688, change `--chip` to `bm1688`. Before compilation, Protected validates the complete
SDK manifest and paired runtime/provisioner hashes; package auditing checks them again.
The default `include` policy requires encrypted preset models; the complete SDK does not
automatically encrypt them. The `preserve` example above retains installed models and cannot
initialize a blank device or establish model-delivery acceptance. To package already prepared
encrypted models, change the policy to `COSMO_PACKAGE_MODELS=include`.

Permission to check in the complete SDK applies only to this fork, not to publication elsewhere
or general relicensing. Guard device certificates and model-encryption secrets remain
controlled inputs and must not enter the public repository.

### RK3576 Protected Build

Use Linux x86_64 / Bash and the same Rockchip Compose entry. The default SDK is
`prebuild/model-guard-v2-rknn-abi1/` (Model Guard v2.4 RKNN ABI 1), containing the matching
header, `libcosmo_model_guard_rknn.so.1.0.0`, provisioning tool, and manifest. Configuration
validates the SDK; package auditing checks the runtime, provisioner, and model policy.
A Sophon SDK cannot replace this RKNN SDK.

This command **updates the application while retaining installed models**. Deployment relies on
the device's existing authorization and models; compilation does not require a device connection:

```bash
COSMO_TARGET_CHIP=rk3576 \
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
COSMO_PACKAGE_MODELS=preserve \
  ./scripts/docker-compose.sh -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

After success, verify and take the package from this directory:

```bash
cat build_output/production-release/rk3576/TARGET_CHIP
(cd build_output/production-release/rk3576 && sha256sum -c SHA256SUMS)
```

This example carries no models and cannot initialize a blank device or establish model-delivery
acceptance. To deliver models in the package, use `COSMO_PACKAGE_MODELS=include` and supply
matching RK3576 encrypted models and resources through `COSMO_RKNN_MODELS_DIR` and
`COSMO_RKNN_RESOURCE_OVERLAY_DIR`. The paths must be accessible inside the container.
The repository's default plaintext `.rknn` models cannot be used directly in a Protected
package; the package audit rejects them.

`COSMO_MODEL_GUARD_SDK_ROOT` may override the RKNN SDK path, subject to the same compatibility
checks. Its accompanying terms govern fork authorization and distribution. Build support does
not establish device acceptance for this run. Changing the chip parameter to RV1126B does
not produce a supported Protected package.

### Builder Images, Cache, and Custom Models

Sophon uses `ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_sophon:v1` from
`docker-compose.sophon.yml`. Its entry script calls `scripts/build.sh -T -c <chip>`.
The Windows PowerShell entry first syncs the source into a Linux volume and restores `.so` symlinks.

Sophon and Rockchip builds in the same working directory share the Compose npm cache.
The first build fills it serially from the lockfile, then installs offline. Removing the
cache volume causes downloads again. Build services use host networking for dependencies
without publishing application ports.

The Rockchip image's builder lock must match the source checkout. RK3576 and RV1126B use
isolated MPP/RGA roots. RV1126B sysroots are sealed to source revisions, ELF properties,
and hashes; path mapping keeps temporary directories out of binaries. Package auditing
checks the pinned upstream MPP Apache-2.0/MIT and RGA `COPYING` licenses.
`docker-compose.rk3576.yml` remains a thin compatibility entry.

For commercial or proprietary RV1126B models, select an independent manifest through
`COSMO_RKNN_ARTIFACT_MANIFEST`; it receives the same chip, size, and hash audit. Keep the
manifest and materials in the private task directory and mount it at a container-accessible
path. `preserve` carries no model weights and cannot initialize a blank device or turn a build
result into acceptance with models on a device.
