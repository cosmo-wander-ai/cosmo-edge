# Aibox Search Tool

Independent Windows Qt/C++ device search and WAN network configuration tool.

This project is intentionally standalone:

- It is under `3rd/searchtool`.
- It is not included by the repository root `CMakeLists.txt`.
- It does not use the ARM/aibox build scripts.
- It depends only on Qt Widgets/Network and CMake.

## Feasibility

The tool is feasible with Qt/C++ because the existing protocol is plain UDP + JSON:

- Search: send `probe` to `239.255.0.0:46000`.
- Receive: parse `probe` `ack`.
- Modify WAN: send legacy `modifyNetCard` to device UDP `46000`.

Qt provides all required pieces:

- `QUdpSocket` for multicast and unicast UDP.
- `QNetworkInterface` for Windows network adapter selection.
- `QJsonDocument/QJsonObject` for protocol JSON.
- `QCryptographicHash` for MD5 uppercase password payload.
- `Qt Widgets` for the desktop UI.

## Current Scope

Implemented:

- Enumerate Windows IPv4 adapters.
- Multicast search by selected adapter.
- Direct target-IP probe.
- Display device IP, SN, model, version, WAN/LAN summary.
- Modify legacy WAN/eth0 network parameters:
  - DHCP/static
  - IP
  - netmask
  - gateway
  - DNS1/DNS2
- Send administrator password as uppercase MD5 by default.

Not implemented yet:

- New `modifyNetCards` protocol.
- LAN/eth1 editing.
- Try/confirm/rollback flow.
- Installer generation.

These should wait until the device-side `SearchMain/SearchMulticast` protocol is upgraded.

## Requirements

Windows build machine:

- CMake 3.21+
- Visual Studio 2022 with C++ Desktop workload
- Qt 6.x or Qt 5.x with MSVC x64 kit

Example Qt prefix:

```text
C:\Qt\6.6.3\msvc2019_64
```

## Build

From this directory:

```powershell
cd D:\project\code\tmp\3rd\searchtool
.\scripts\build-windows.ps1 -QtPrefix "C:\Qt\6.6.3\msvc2019_64"
```

If Qt is already discoverable through `CMAKE_PREFIX_PATH` or your environment:

```powershell
.\scripts\build-windows.ps1
```

The executable is generated at:

```text
3rd\searchtool\build\Release\AiboxSearchTool.exe
```

## Package Runtime DLLs

After building:

```powershell
.\scripts\package-windows.ps1 -QtPrefix "C:\Qt\6.6.3\msvc2019_64"
```

This copies the executable to:

```text
3rd\searchtool\dist
```

and runs `windeployqt` to copy Qt runtime DLLs.

## Usage

1. Start `SearchMain` or `aibox-search` on the device.
2. Run `AiboxSearchTool.exe`.
3. Select the Windows adapter in the same network as the device.
4. Click `组播搜索`.
5. If multicast is blocked, enter the known device IP and click `指定 IP 探测`.
6. Select a device.
7. Edit WAN/eth0 settings.
8. Enter administrator password.
9. Click `应用到选中设备`.

The current device-side legacy protocol only reliably supports WAN/eth0 modification. LAN/eth1 editing is disabled in the UI for now.

## Network Notes

The current device-side service replies to multicast first, so the Windows firewall may need to allow inbound UDP `46000`.

If search fails:

- Check that the selected adapter IP is in the device network.
- Check Windows firewall inbound UDP `46000`.
- Check that the device process is listening on UDP `46000`.
- Try target-IP probe instead of multicast search.

