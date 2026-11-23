---
title: Deployment Guide
description: Runtime directories, service processes, ports, upgrade packages, and systemd behavior.
prev:
  text: macOS Docker Preview
  link: /en/guide/macos-docker-preview
next:
  text: Runtime Configuration
  link: /en/guide/configuration
---

# Deployment Guide

This page is organized according to the current runtime scripts, mainly covering:

- `scripts/docker-entrypoint.x86.sh`
- `scripts/start.sh`
- `scripts/run_start.sh`
- `scripts/install.sh`

Packaged `scripts/install.sh` is generated from `scripts/legacy_migration_install.sh`.
First obtain the matching artifact through the [Build Guide](./build.md), then select an entry below.

| Target platform | Default Open package directory | Default persistent-data root | Deployment entry |
| --- | --- | --- | --- |
| x86 Linux / Windows | `build_output/` (exported at runtime) | `/data/cwaiuserdata` in the container, mounted from a named volume | Rebuild the corresponding Compose service |
| Apple Silicon macOS Preview | `build_output/macos-x86/` | `/data/cwaiuserdata` in the container, with separate named volumes | Mac Preview script |
| Sophon BM1688 | `build_output/public-runtime/bm1688/` | `/data/cwaiuserdata` | SSH installation below or device web upgrade |
| Sophon CV186X | `build_output/public-runtime/cv186x/` | `/data/cwaiuserdata` | SSH installation below or device web upgrade |
| Rockchip RK3576 | `build_output/rk3576/` | `/userdata/cwaiuserdata` | SSH installation below |
| Rockchip RV1126B | `build_output/rv1126b/` | `/userdata/cwaiuserdata` | SSH installation below |

Protected is supported only for BM1688, CV186X, and RK3576, with output under
`build_output/production-release/<chip>/`. See [advanced build options](./build.md#advanced-build-options)
for SDK, model, and authorization prerequisites. The default application directory is
`/appfs/cosmo_wander/cwai_data`. Select by chip and profile, not by similar filenames.
An installation method does not establish device acceptance for every platform; consult the release record.

## x86 Docker Runtime

Run these commands at the repository root on the host. For initial preparation, see the
[Build Guide](./build.md#x86-docker-development-runtime).

Start:

- **Linux**:
  ```bash
  ./scripts/docker-compose.sh -f docker-compose.x86.yml up -d --build
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh up
  ```

Stop:

- **Linux**:
  ```bash
  ./scripts/docker-compose.sh -f docker-compose.x86.yml down
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml down
  ```
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh down
  ```

View logs:

- **Linux**:
  ```bash
  ./scripts/docker-compose.sh -f docker-compose.x86.yml logs -f
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml logs -f
  ```
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh logs --follow
  ```

To update Linux/Windows, run `up -d --build` again from the same working directory and Compose
project. On Mac, use `./scripts/macos-docker-preview.sh up --build`. Save snapshots of both the
user-data and resource volumes before updating. Retain both volumes and do not run `down -v`.
Existing resource volumes do not update automatically with a new image.

Confirm the container is running, the management page responds, the software version matches
the build, and existing users, tasks, and models remain readable. Recreating a container does
not require a changed host `bootId`; the container does not use `cosmo.service`.

## Runtime Directories

| Path | Description |
| --- | --- |
| `<INSTALLPATH>` | Main installation directory, set by the Dockerfile or deployment scripts |
| `<INSTALLPATH>/resource` | Runtime resource directory |
| `<DATADIR>` | User persistent data directory, by default located on a persistent volume |
| `<DATADIR>/log/logs` | Log directory |
| `<DATADIR>/upload/sessions` | Resumable chunk-upload sessions; directory mode is fixed to `0700` |
| `<DATADIR>/upgrade` | Upgrade package directory |

## Runtime Processes

The startup scripts launch:

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

Corresponding paths:

`${INSTALLPATH}` is set by the `INSTALLPATH` environment variable in the Dockerfile (default value see Runtime Configuration).
Specific paths:
```text
/usr/sbin/nginx  (system nginx)
${INSTALLPATH}/bin/srs
${INSTALLPATH}/bin/cosmo-engine
```

## Default Ports

| Port | Source | Purpose |
| --- | --- | --- |
| `8080 -> 80` | x86 Compose files; the Mac Preview binds only to `127.0.0.1` | x86 Docker web console |
| `1936` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | RTMP |
| `1985` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | SRS API |
| `18088` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | HTTP stream |
| `8000` | `src/app/AppConstants.h` (`kDefaultHttpPort`) | Backend HTTP |
| `9000` | `src/app/AppConstants.h` (`kDefaultWebSocketPort`) | Backend WebSocket |

`8080->80`, `1936`, `1985`, and `18088` are exposed to the host. `8000` and `9000` are in-container process ports. In `docker-compose.x86.yml`, `8000` is mapped as `8000:8000/udp` for device discovery, which is distinct from the backend HTTP (TCP). The host accesses the API through nginx, which reverse-proxies from in-container `80` to host `8080`.

The production UDP discovery protocol accepts only `probe` queries. Network-card changes, hardware-information writes, and authorization-code operations are no longer executed over multicast: use an implemented authenticated management API, while operations without a secure replacement API are rejected.

Stream environment variables set by the runtime scripts:

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## Release Package Structure

The web upgrade entry and boot-time upgrade script require `bin`, `files`, `font`, `lib`,
`scripts`, and `web` under the package root. `lib` is mandatory. The upgrade entry's directory
check does not require `resource`.

Release builds also undergo `scripts/verify_package_contents.py` auditing: they must retain
`resource` in addition to those directories. The audit checks executables, runtime-path
declarations, licenses, target chip, and Open/Protected policy. `COSMO_PACKAGE_MODELS=preserve`
omits model weights, not the required release-package directories.

The upgrade package filename must match this pattern:

```text
cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz
```

The web console performs a local upgrade as follows:

1. Query device status and record the current Linux `bootId`.
2. Transfer the package in chunks according to live device capabilities while showing actual upload progress.
3. Validate the filename, MD5, archive safety, package layout, and live disk budget.
4. After a Sophon reboot, the startup script revalidates the MD5 and installs the package. Open and Protected packages permanently use this same upgrade flow.
5. Return to login after observing a new `bootId`. If reboot invalidates the login session, first observe the device offline and then require an authentication response from the recovered service before returning to login.

The 15-minute recovery wait is a UI timeout; it does not cancel an upgrade already running on the device. Keep power connected and inspect device networking and systemd logs if it expires. After signing in again, verify the software version against the release package; UI recovery proves reboot and service recovery, not version acceptance.

## Upgrade from 1.0.0 to 1.1.0

### Supported paths

The public v1.0.0 device platform was BM1688, with x86 Linux/Windows available
as developer modes. Do not describe platforms introduced in v1.1 as in-place
upgrades from v1.0.0:

| Current environment | Path to v1.1.0 | Data handling |
| --- | --- | --- |
| BM1688 v1.0.0 | In-place upgrade with the BM1688 Open package or corresponding controlled Protected package | Continues to use `/data/cwaiuserdata` |
| x86 Linux/Windows v1.0.0 | Rebuild the Compose service from the `v1.1.0` source | Preserve the existing named volumes; do not run `down -v` |
| CV186X | Fresh installation of the v1.1.0 CV186X package | No public v1.0.0 CV186X upgrade baseline exists |
| RK3576 / RV1126B | Fresh installation for release users; devices that ran prerelease builds follow the data-root migration below | v1.1 uses `/userdata/cwaiuserdata` |
| Apple Silicon macOS | Start fresh through the Preview guide | No v1.0.0 macOS release baseline exists |

This section defines the upgrade method; it does not by itself prove that a
candidate package passed release acceptance. Maintainers must bind the final
commit, package SHA-256, device identity, and upgrade result in one evidence
record before releasing an artifact.

### Pre-upgrade checks

The package, `bootId`, and systemd steps below apply to edge devices. For x86, back up and
rebuild through the [Docker runtime path](#x86-docker-runtime), then perform the container
and business checks below.

1. Obtain the archive from the target chip directory together with `TARGET_CHIP`
   and `SHA256SUMS`. Verify both the chip marker and SHA-256; never reuse a full
   package across chips.
2. Record the current `bin/version.txt`, `bootId`, service state, device/firmware
   identity, and target package SHA-256:

   ```bash
   cat /appfs/cosmo_wander/cwai_data/bin/version.txt
   cat /proc/sys/kernel/random/boot_id
   systemctl status cosmo.service --no-pager
   ```

3. Complete or cancel active uploads in the web console. An incomplete v1.0.0
   upload is not a resumable session and is not migrated across reboot. After
   upgrade, verify v1.1 staged-upload creation, resume, consumption, and cancel.
4. Stop the service and back up the current data root to a different filesystem;
   verify that the backup can be listed. Do not place the backup inside the data
   root being archived, and keep the old package and data until acceptance ends.
5. Ensure the destination filesystem can hold the persistent data, archive, and
   runtime headroom. For a device that ran an early Rockchip build, also run:

   ```bash
   findmnt -T /userdata
   du -sk /data/cwaiuserdata
   df -Pk /userdata
   ```

   `/userdata` must be a writable mount. The migrator requires free space equal
   to the selected persistent data plus 64 MiB. It refuses to use an ordinary
   `/userdata` directory that falls back to the root filesystem.

### Rockchip data-root migration

An RK3576 or RV1126B package performs a one-time migration from
`/data/cwaiuserdata` to `/userdata/cwaiuserdata` before replacing the application:

- It transactionally copies configuration, configuration backups, databases,
  database backups, camera configuration, user models, image libraries, events,
  and other persistent top-level entries. The `/data/cwaiuserdata` source remains
  available for recovery.
- It excludes the rebuildable or transient `cwai`, `log`, `runtime`, `temporary`,
  `tmp`, `upload`, `upgrade`, and `web` entries and upgrade markers.
- A successful copy creates
  `/userdata/cwaiuserdata/.cosmo-data-root-migration-v1` with mode `0600`, so a
  later package cannot overwrite newer target data with the retained source.
- If the installed runtime already declares `/userdata/cwaiuserdata` and it has
  persistent entries, that root remains authoritative and stale `/data` contents
  are not recopied. An upgrade marker, logs, or temporary directories alone do
  not count as persistent state and cannot hide a legacy database.
- If both roots contain state and the installer cannot prove which one is active,
  it fails before stopping the service or replacing the application. Do not merge
  the two databases manually.

The application tree and a newly copied data root remain one transaction until
the installer returns. A copy or installation failure restores the old
application and removes the uncommitted target root. Installer success confirms
the file transaction, not post-start service health.

### Installation and post-upgrade acceptance

**Edge devices:** BM1688 can use the web flow above or SSH installation below. For other chips,
use SSH with the matching package directory. After reboot, confirm a changed `bootId`,
`cosmo.service` in the `active` state, and the version matching the target package.
Inspect the service with `systemctl status cosmo.service --no-pager`.

**x86 Docker:** Retain the original user-data and resource volumes and rebuild through the
matching Compose entry. Use that entry's `ps` command to inspect the container, then verify
management-page responses, software version, and retained data. Device bootId/systemd checks do not apply.

Both environments need business checks:

1. Existing users, cameras, tasks, parameters, user models, and event history remain readable.
2. Run one real video task and confirm inference, preview, and at least one alarm with media.
3. Create a staged upload, recover the session after a service restart, and verify cleanup after consumption or cancellation.
4. Reboot the device or restart the x86 container, then recheck sign-in, tasks, models, history, and upload state.
5. Retain before/after data inventories, service logs, and explicit PASS/FAIL results.

`bash test/test_legacy_migration_installer.sh` covers data copying, transient
exclusions, idempotence, conflict rejection, and transaction rollback. It is a
host-filesystem test and does not replace final-package upgrade, inference, and
alarm evidence from a real device.

### Recovery boundary

An error before the installer returns restores the old application, and a
Rockchip migration retains the old data root. If the new service fails real
business acceptance after the installer returned, this version does **not**
automatically roll back based on HTTP health. Keep power connected, save
`systemctl status cosmo.service` and the logs, reinstall the previous package
whose SHA-256 was recorded, and restore its data root from the pre-upgrade backup.
Do not delete `/data/cwaiuserdata`, `/userdata/cwaiuserdata`, or the backup before
recovery is complete.

## Upgrade from 1.1.0 to 1.2.0

Public Open packages are built separately for BM1688, CV186X, RK3576, and RV1126B.
Use the package matching the device chip. This release run checks installation,
upgrade, and rollback on a BM1688 test device only. Build and package audits for
the other chips do not establish a device-upgrade result; consult the release
manifest for the recorded scope.

Keep the old installation package and a data-recovery path. Before upgrading:

1. Check `TARGET_CHIP` and `SHA256SUMS` in the selected 1.2.0 chip directory, and
   verify that the MD5 in the archive name matches its contents. Do not mix chips
   or Open and Protected package profiles.
2. Record the old `bin/version.txt`, service status, actual persistent-data root,
   and an inventory of configuration, database records, user models, and resources.
   Complete or cancel uploads in progress.
3. Stop the service, back up persistent data to a separate filesystem, and verify
   that the backup is readable. Keep the old package. Back up custom models or
   templates that share paths with the new package: by default, the new files
   replace old files at the same paths. `CLEAN_RESOURCE=1` instead keeps only
   packaged resources.

On BM1688, use the Web upgrade flow described above or the SSH installation
path below. After installation, check the version, `cosmo.service`, access to the
management endpoint, and preservation of configuration, database records, and
persistent files. Recheck after another reboot. The Web page's 15-minute wait
ending does not stop an upgrade already running on the device; inspect the
service and logs before taking recovery action.

If the new service cannot start after installation returns, retain its logs,
reinstall the verified old package through SSH, and restore the matching
pre-upgrade data snapshot. Check the restored version, service, and data. Do not
assume the old program can read data newly created by 1.2.0. Bind any upgrade
or rollback result to the candidate commit, package SHA-256, and target device.

For x86 Linux/Windows, update the Compose service while retaining both user-data
and application-resource volumes; do not use `down -v`. An existing resource
volume does not update automatically with the image. Save the corresponding
volume snapshots before updating or restoring the separate Apple Silicon
macOS Docker Preview environment.

## SSH Installation Path

Use this entry for Sophon / Rockchip devices with a prepared Linux base system, systemd,
nginx, Python 3, pgrep, and the target platform's runtime dependencies. Rockchip requires
`/userdata` to be a writable persistent mount. The application package does not install an
operating system or automatically prepare device runtime dependencies.

Record the current version, service state, and recovery plan first. Back up the actual data
root and custom resources that must be retained. Follow the
[upgrade and recovery boundaries](#upgrade-from-1-1-0-to-1-2-0) above. The installer replaces
the application and creates or replaces and enables `cosmo.service`. Reboot after installation succeeds.

### 1. Build Host: Verify and Transfer

This Bash example selects **BM1688 Open**. For another target, change `package_dir` to the
matching directory in the table above. Replace `device` with the actual SSH account and
address. First match `TARGET_CHIP` to the device identity, then transfer. Do not execute an
unverified downloaded package.

```bash
package_dir='build_output/public-runtime/bm1688'
device='root@<device_ip>'
(
  set -e
  cd "$package_dir"
  cat TARGET_CHIP
  sha256sum -c SHA256SUMS
  package_name=$(awk 'NR == 1 {print $2}' SHA256SUMS)
  printf 'Package: %s\n' "$package_name"
  scp "$package_name" TARGET_CHIP SHA256SUMS "${device}:/tmp/"
)
```

For a Windows Sophon build, first complete the
[PowerShell verification](./build.md#hand-a-sophon-build-to-the-deployment-workflow).
In that same terminal, reuse the verified `$outputDir` and `$packageName`:

```powershell
$device = 'root@<device_ip>'
scp (Join-Path $outputDir $packageName) (Join-Path $outputDir 'TARGET_CHIP') (Join-Path $outputDir 'SHA256SUMS') "${device}:/tmp/"
if ($LASTEXITCODE -ne 0) { throw 'Package transfer failed' }
```

After a successful transfer, connect from the build host (Bash or PowerShell):

```sh
ssh "$device"
```

### 2. Device: Install and Reboot

Run this block in the **device root session** opened above. Confirm `TARGET_CHIP` again.
Use the transferred manifest's single actual filename, not an old-version wildcard.
Any failed step exits the block without proceeding to reboot.

```bash
(
  set -e
  cd /tmp
  cat TARGET_CHIP
  sha256sum -c SHA256SUMS
  package_name=$(awk 'NR == 1 {print $2}' SHA256SUMS)
  install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
  tar -xzf "$package_name" -C "$install_dir"
  cd "$install_dir"/cosmo-V*/
  ./scripts/install.sh
  reboot
)
```

Reconnect after reboot, verify the version and service, and complete
[post-upgrade acceptance](#installation-and-post-upgrade-acceptance).
Installer success confirms the file transaction only. If service or business acceptance fails,
follow this page's recovery procedure.

## systemd Service

The configured device uses this service start command:

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

Packaged `scripts/install.sh` creates or replaces and enables `cosmo.service` during both
fresh installation and upgrade, and installs the log-cleanup service. The application service
runs as `root` with `Restart=on-failure` and starts on reboot after installation. The device's
base system and runtime dependencies must already exist; the installer does not prepare an OS.

Some Sophon images restore the persistent data tree to the appliance administrator at boot. The upload staging service therefore allows `sessions` to inherit the owner of an immediate parent that is not writable by group/other, while still requiring:

- a real, non-symlink `sessions` directory with mode `0700`;
- stable owner, device, and inode identity for the lifetime of the process;
- service-owned private session directories and payloads.

Do not apply broad recursive `chmod` or `chown` operations to the complete `<DATADIR>`.

## Interface Documentation Static Links

Packaged interface files:

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

Linked at runtime to:

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`
