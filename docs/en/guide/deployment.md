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

## x86 Docker Runtime

Start:

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
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
  docker compose -f docker-compose.x86.yml down
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

The install/upgrade scripts expect the release package to contain:

- `bin`
- `files`
- `font`
- `scripts`
- `web`

Optional or handled by presence:

- `lib`
- `resource`

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

BM1688 can use the web flow above or the SSH entry below. x86 rebuilds through
the corresponding Compose file while keeping its named volumes. After upgrade,
complete at least these checks:

1. `bootId` changed, `cosmo.service` is `active`, and the software version matches
   the target package.
2. Existing users, cameras, tasks, parameters, user models, and event history are
   readable.
3. Run one real video task and confirm inference, preview, and at least one alarm
   with media.
4. Create a staged upload, recover the session after restart, verify cleanup after
   successful consumption, and verify the cancel cleanup path.
5. Reboot once more and repeat sign-in, task, model, history, and upload-state checks.
6. Retain before/after data inventories, service logs, and explicit PASS/FAIL results.

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

## SSH Installation Path

In addition to web upgrade, packaged `scripts/install.sh` is the SSH entry point
for migration from main and later compatible installations. It installs the
application, replaces and enables `cosmo.service`, and relies on reboot to start
the service:

```bash
scp build_output/public-runtime/<chip>/<package>.tar.gz root@<device_ip>:/tmp/
ssh root@<device_ip>
cd /tmp
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf <package>.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
sudo ./scripts/install.sh
sudo reboot
```

This path assumes that the Sophon Linux base system and runtime dependencies are
already prepared. It is not an OS-image installation procedure for arbitrary
blank hardware. Record the current version and recovery plan first; the
installer replaces the active application tree.

## systemd Service

The configured device uses this service start command:

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

`scripts/install.sh` implements the upgrade transaction; it does not create the
systemd unit for a blank device. The service runs as `root` with
`Restart=on-failure`.

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
