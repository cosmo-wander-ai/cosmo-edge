---
title: Troubleshooting
description: Common build, runtime, port, Sophon image, log, and documentation-site issues.
prev:
  text: Runtime Configuration
  link: /en/guide/configuration
next:
  text: Architecture Overview
  link: /en/guide/architecture
---

# Troubleshooting

This page collects the most common build and runtime issues for the current project.

## Web Console Cannot Open

Confirm that you are using host port `8080`:

```text
http://127.0.0.1:8080
```

Check container status:

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml ps
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml ps
  ```

- **Apple Silicon macOS (Preview)**:

  ```bash
  ./scripts/macos-docker-preview.sh status
  ```

View logs:

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml logs -f
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml logs -f
  ```

- **Apple Silicon macOS (Preview)**:

  ```bash
  ./scripts/macos-docker-preview.sh logs --follow
  ```
## Windows x86 Docker Troubleshooting

If you are setting up `cosmo-edge` on Windows using the x86 Docker configuration, check the following:

1. **Docker Desktop:** Make sure Docker Desktop is running and the Docker Engine has finished starting. If commands time out or report daemon errors, check that Docker Desktop is running before continuing.

2. **Docker Compose V2:** Use the Compose V2 command format `docker compose` instead of the older `docker-compose` command.

3. **Port 8080:** The Windows x86 setup uses host port `8080` for the web console. If Docker reports a port binding error, see [Port Conflicts](#port-conflicts) for Windows-specific checks and instructions for changing the host port.

4. **Windows x86 configuration:** Use `docker-compose.x86.windows.yml` when starting the Windows x86 setup:

   ```powershell
   docker compose -f docker-compose.x86.windows.yml up -d --build
5. **Docker logs**: To view the x86 Docker logs, run:
   ```powershell 
   docker compose -f docker-compose.x86.windows.yml logs -f
   
## Port Conflicts

The x86 Compose file publishes:

- `8080`
- `1936`
- `1985`
- `18088`
- `8000/udp`

If a port is occupied, you can modify the host port in `docker-compose.x86.yml`, or stop the service that occupies the port.

On Windows, Hyper-V / WSL can reserve a TCP port range. Docker may therefore report a bind failure even when `netstat` shows no listening process. Check the reserved ranges first:

```powershell
netsh interface ipv4 show excludedportrange protocol=tcp
```

`docker-compose.x86.windows.yml` accepts `COSMO_X86_WEB_PORT`, so you can change the web host port without editing a tracked file. For example, use `8280`:

```powershell
$env:COSMO_X86_WEB_PORT = "8280"
docker compose -f docker-compose.x86.windows.yml up -d --build
```

Then open `http://127.0.0.1:8280`. The default remains `8080` when the variable is unset.

The Mac Preview accepts the same web-port variable while retaining its host-only binding:

```bash
COSMO_X86_WEB_PORT=8280 ./scripts/macos-docker-preview.sh up
```

If Mac builds are unexpectedly slow, inspect Docker Desktop's VMM and Rosetta
settings. See [macOS Docker Preview](./macos-docker-preview.md) for the full boundary.

## Windows Build Scripts Report `No such file or directory`

If a Docker build reports that an existing `configure`, `config`, or `Configure` file cannot be executed, Git for Windows may have checked out the extensionless script with CRLF endings. The container then cannot parse its shebang.

The root `.gitattributes` pins automatically detected text files, including these extensionless scripts, to LF. After pulling the latest rules, retry from a fresh clone or clean worktree with no uncommitted changes. Confirm the rules with:

```powershell
git check-attr text eol -- 3rd/mp4v2-2.0.0/configure 3rd/openssl-3.5.3/config 3rd/srs-6.0-r0/trunk/configure
```

All three files should report `text: auto` and `eol: lf`.

## No Build Artifact in `build_output/`

Use the full run command:

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

The [Build Guide](./build.md#sophon-artifacts) is the authoritative reference for
the Sophon entry point, profiles, and output contract. For example, after the
default BM1688 Open build, inspect the chip-scoped directory directly:

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package
cat build_output/public-runtime/bm1688/TARGET_CHIP
(cd build_output/public-runtime/bm1688 && sha256sum -c SHA256SUMS)
```

Sophon output is not written directly to `build_output/`. Each
`build_output/<profile>/<chip>/` directory should contain `TARGET_CHIP`,
`SHA256SUMS`, and exactly one `cosmo-V<version>-<32-char-md5>.tar.gz`. First check
that the selected profile and chip match the directory being inspected.

Do not substitute `docker compose build` for the `run` entry point above; it does
not execute the container command that exports the artifact.

## Sophon Build Failure

The `cosmo-sophon-package` service directly uses the pre-built GHCR image
configured in `docker-compose.sophon.yml`; there is no local `Dockerfile.sophon`
build path. The [Build Guide](./build.md#sophon-artifacts) is the single reference
for the current image and build chain.

If the build fails, rerun the same entry point and inspect the final log lines:

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x 2>&1 | tail -50
```

For BM1688, replace the final argument with `bm1688` or omit it.

Common causes:

- Failure to pull the prebuilt GHCR image or populate the npm cache — check Docker registry networking, proxy, DNS, and the current build log.
- Insufficient disk space — the build requires approximately 3GB.
- An unsupported `COSMO_MODEL_GUARD_BUILD_PROFILE` value — only
  `public-runtime` and `production-release` are accepted.
- An unsupported chip model — only `bm1688` and `cv186x` are accepted; omitting
  it defaults to `bm1688`.
- Selecting `production-release` outside the controlled release environment —
  missing production SDK, provisioning, release-public-key, or bootstrap inputs is
  rejected by design. Use the default Open profile (internal name `public-runtime`)
  for ordinary source-code builds; do not bypass the formal release checks.

## Protected Presets Do Not Load

The device needs exactly one Guard state file:

```text
/data/cwaiuserdata/model-guard/device-certificate.bin
```

Check certificate status and service logs first:

```bash
sudo test -f /data/cwaiuserdata/model-guard/device-certificate.bin
sudo journalctl -u cosmo.service -b --no-pager -n 200
```

If the controlled provisioner is still present in its temporary device
directory, run `sudo /temporary-directory/cosmo-model-provision status` to
validate the certificate against the live device. The Open package does not
provide that tool.

- `-2001` (`CMG_V2_CERTIFICATE_UNAVAILABLE`) means the certificate is missing
  or unreadable.
- `-2002` (`CMG_V2_CERTIFICATE_REJECTED`) means the certificate is malformed,
  has an invalid signature, or was issued for another device.

Do not create per-model licenses or copy another device's certificate. Create
a fresh request on this device, issue its certificate in the controlled
offline environment, and run
`cosmo-model-provision install --certificate <absolute-certificate-path>`.
The Open installer does not create, delete, or repair this certificate.

## nginx / SRS / cosmo-engine Not Started

Run the script:

```text
${INSTALLPATH}/scripts/run_start.sh
```

The startup sequence includes:

1. Stop existing processes.
2. Start nginx.
3. Start SRS.
4. Start `cosmo-engine`.

Check the logs:

```text
/data/cwaiuserdata/log/logs
```

## Upgrade Page Keeps Waiting

The device goes offline during an upgrade. The page waits for a new Linux `bootId` and stops the UI wait after 15 minutes. If reboot clears the login session, the page returns to login only after it observed an offline interval and the recovered service answers at the authentication boundary. This UI timeout does not cancel the device-side upgrade; verify the software version after signing in again.

On a Sophon device, inspect:

```bash
systemctl status cosmo --no-pager -l
journalctl -u cosmo -b --no-pager -n 200
```

Normally `cosmo.service` is `active (running)`. A fatal initialization
exception exits non-zero so `Restart=on-failure` can retry.

## Documentation Site Build Fails

First install dependencies:

```bash
npm ci
```

Then build:

```bash
npm run docs:build
```

In Windows PowerShell, if you encounter an `npm.ps1` execution-policy issue, you can use:

```powershell
npm.cmd run docs:build
```

## `vitepress` Not Found

This means the documentation-site dependencies have not been installed:

```bash
npm ci
```

## npm Audit Reports Vulnerabilities

The current documentation-site dependencies may trigger npm audit warnings. Do not blindly upgrade dependencies; before upgrading, confirm that VitePress, the theme configuration, and the GitHub Pages workflow still build successfully.

## Windows Native CPU Build

There is currently no confirmed-working Windows native CPU build script in this repository. Do not present old scripts or old commands as a publicly supported path.
