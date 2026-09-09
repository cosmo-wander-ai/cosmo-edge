---
title: 部署指南
description: 当前运行目录、服务进程、端口、升级包和 systemd 行为。
prev:
  text: macOS Docker Preview
  link: /guide/macos-docker-preview
next:
  text: 架构概览
  link: /guide/architecture
---

# 部署指南

本文根据当前运行脚本整理，主要涉及：

- `scripts/docker-entrypoint.x86.sh`
- `scripts/start.sh`
- `scripts/run_start.sh`
- `scripts/install.sh`

## x86 Docker 运行环境

启动：

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

停止：

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

查看日志：

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

## 运行目录

| 路径 | 说明 |
| --- | --- |
| `<INSTALLPATH>` | 主安装目录，由 Dockerfile 或部署脚本设定 |
| `<INSTALLPATH>/resource` | 运行资源目录 |
| `<DATADIR>` | 用户持久化数据目录，默认位于持久化卷上 |
| `<DATADIR>/log/logs` | 日志目录 |
| `<DATADIR>/upload/sessions` | 可恢复分片上传会话；目录权限固定为 `0700` |
| `<DATADIR>/upgrade` | 升级包目录 |

## 运行进程

启动脚本会拉起：

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

对应路径：

`${INSTALLPATH}` 由 Dockerfile 中的 `INSTALLPATH` 环境变量设置（默认见运行配置）。
具体路径：
```text
/usr/sbin/nginx  (system nginx)
${INSTALLPATH}/bin/srs
${INSTALLPATH}/bin/cosmo-engine
```

## 默认端口

| 端口 | 来源 | 用途 |
| --- | --- | --- |
| `8080 -> 80` | x86 Compose 文件；Mac Preview 仅绑定 `127.0.0.1` | x86 Docker Web 控制台 |
| `1936` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | RTMP |
| `1985` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | SRS API |
| `18088` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | HTTP stream |
| `8000` | `src/app/AppConstants.h`（`kDefaultHttpPort`，TCP） | 后端 HTTP 常量（容器内监听 TCP） |
| `9000` | `src/app/AppConstants.h`（`kDefaultWebSocketPort`，TCP） | 后端 WebSocket（容器内监听 TCP） |

> 端口暴露说明：`8080 -> 80`、`1936`、`1985`、`18088` 是 x86 Docker 对**主机暴露**的端口。`8000`、`9000` 是容器内进程端口；其中 `8000` 在 `docker-compose.x86.yml` 中以 `8000:8000/udp` 形式映射到主机（用于设备发现等 UDP 场景），与后端 HTTP 的 TCP 监听不同。主机侧访问后端 HTTP/WebSocket API 通常经由 nginx（容器内 `80`，映射到主机 `8080`）反向代理，而不是直接访问主机的 `8000`。

生产环境的 UDP 设备发现协议仅允许 `probe` 查询。修改网卡、写入硬件信息和授权码操作不再通过多播执行；只能通过已实现的身份验证管理 API 调用，尚未提供安全替代 API 的操作将被拒绝。

运行脚本设置的流媒体环境变量：

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## 发布包结构

安装/升级脚本期望发布包中包含：

- `bin`
- `files`
- `font`
- `scripts`
- `web`

可选或按存在处理：

- `lib`
- `resource`

升级包文件名必须匹配以下格式：

```text
cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz
```

Web 控制台的本地升级流程如下：

1. 查询设备状态并记录当前 Linux `bootId`。
2. 按设备返回的上传能力分片传输安装包，界面显示实际上传百分比。
3. 后端校验文件名、MD5、归档安全、目录结构和实时磁盘预算。
4. Sophon 设备重启后，启动脚本再次校验 MD5 并安装。Open 与 Protected 包永久使用同一升级流程。
5. 页面在看到新的 `bootId` 后返回登录页。如果重启使登录会话失效，则必须先观察到设备离线，再收到新服务的鉴权响应，才能判定服务已恢复并返回登录页。

页面等待恢复的 15 分钟是交互超时，不会中止设备端已经开始的升级。超时后应保持供电，并通过设备网络和 systemd 日志确认状态。重新登录后还应核对软件版本与本次发布包；页面恢复只证明重启与服务恢复，不替代版本验收。

## 从 1.0.0 升级到 1.1.0

### 支持范围

公开的 v1.0.0 设备平台只有 BM1688，另有 x86 Linux/Windows 开发模式。不要把
v1.1 新增平台描述成从 v1.0.0 原位升级：

| 当前环境 | 到 v1.1.0 的路径 | 数据处理 |
| --- | --- | --- |
| BM1688 v1.0.0 | 使用 BM1688 Open 或对应受控 Protected 包原位升级 | 继续使用 `/data/cwaiuserdata` |
| x86 Linux/Windows v1.0.0 | 从 `v1.1.0` 源码重新构建 Compose 服务 | 保留原命名卷；不要运行 `down -v` |
| CV186X | 全新安装 v1.1.0 CV186X 包 | 没有公开的 v1.0.0 CV186X 升级基线 |
| RK3576 / RV1126B | 正式用户全新安装；曾运行发布前构建的设备按下述数据根迁移处理 | v1.1 使用 `/userdata/cwaiuserdata` |
| Apple Silicon macOS | 按 Preview 指南全新启动 | 没有 v1.0.0 macOS 发布基线 |

本节定义升级方法，但不单独证明某个候选包已经通过发布验收。维护者必须把最终
commit、package SHA-256、设备和升级结果绑定到同一份证据，才能放行正式包。

### 升级前检查

1. 从目标芯片目录取得安装包，同时保留相邻的 `TARGET_CHIP`、`SHA256SUMS`，确认
   芯片标记和 SHA-256；不要跨芯片复用完整包。
2. 记录当前 `bin/version.txt`、`bootId`、服务状态、设备/固件身份和目标包 SHA-256：

   ```bash
   cat /appfs/cosmo_wander/cwai_data/bin/version.txt
   cat /proc/sys/kernel/random/boot_id
   systemctl status cosmo.service --no-pager
   ```

3. 在 Web 控制台完成或取消正在进行的上传。v1.0.0 的未完成上传不是可恢复会话，
   不会跨重启迁移；升级后再验证 v1.1 的 staged upload 创建、续传、消费和取消。
4. 停止服务后，把当前数据根备份到另一个文件系统，并验证备份可以列出。不要把备份
   写回被备份的数据根，也不要删除旧包和旧数据，直至升级验收完成。
5. 确认目标文件系统同时容纳持久化数据、安装包和运行余量。曾运行早期 Rockchip
   构建的设备还要检查：

   ```bash
   findmnt -T /userdata
   du -sk /data/cwaiuserdata
   df -Pk /userdata
   ```

   `/userdata` 必须是可写挂载点。迁移器要求目标剩余空间不小于待复制持久化数据加
   64 MiB；不能把根文件系统上的普通 `/userdata` 目录当成持久化分区。

### Rockchip 数据根迁移

面向 RK3576 或 RV1126B 的包会在替换应用前处理从 `/data/cwaiuserdata` 到
`/userdata/cwaiuserdata` 的一次性迁移：

- 事务复制配置、配置备份、数据库、数据库备份、摄像头配置、用户模型、图片库、
  事件和其他持久化顶层内容；原 `/data/cwaiuserdata` 保留为恢复来源。
- 不复制 `cwai`、`log`、`runtime`、`temporary`、`tmp`、`upload`、`upgrade`、`web`
  和升级标记等可重建或瞬时内容。
- 复制完成后写入权限为 `0600` 的
  `/userdata/cwaiuserdata/.cosmo-data-root-migration-v1`，后续安装不会重新覆盖新数据。
- 如果已安装版本明确使用 `/userdata/cwaiuserdata` 且其中已有持久化内容，继续以该
  目录为准，不用陈旧的 `/data` 内容覆盖它；仅有升级标记、日志或临时目录不算
  持久化内容，不能阻止旧数据库迁移。
- 如果两个数据根都包含内容，但无法证明哪个目录正在使用，安装器会在停止服务和
  替换应用之前失败；不要人工合并两个数据库。

应用目录和新数据根在安装器返回前属于同一个事务。复制或安装失败会恢复原应用并
移除本次未提交的新数据根。安装器成功返回只表示文件事务完成，不表示新服务已经
通过启动健康检查。

### 安装和升级后验收

BM1688 可以使用前述 Web 升级流程，也可以使用下一节的 SSH 入口。x86 使用对应
Compose 文件重新构建服务并保留原命名卷。升级后至少完成以下检查：

1. `bootId` 已变化，`cosmo.service` 为 `active`，软件版本和目标包一致；
2. 原用户、摄像头、任务、参数、用户模型和事件历史仍可读取；
3. 启动一个真实视频任务，确认推理、预览和至少一个带媒体的告警；
4. 创建 staged upload，会话重启后可恢复，成功消费后被清理，取消路径也能清理；
5. 再重启一次，重复登录、任务、模型、历史和上传状态检查；
6. 保留升级前后数据清单、服务日志和 PASS/FAIL 结果。

仓库测试 `bash test/test_legacy_migration_installer.sh` 验证数据复制、瞬时目录排除、
幂等、冲突拒绝和事务回滚；它是主机文件系统测试，不能替代最终包在真实设备上的
升级、推理和告警证据。

### 恢复边界

安装器在返回前发生错误时会恢复旧应用；Rockchip 迁移也会保留旧数据根。安装器
返回后，如果新服务无法通过实际业务验收，当前版本**不会**根据 HTTP 健康状态自动
回滚。保持设备供电，先保存 `systemctl status cosmo.service` 和日志，再通过 SSH
重新安装已记录 SHA-256 的旧包，并按升级前备份恢复其数据根。恢复完成前不要删除
`/data/cwaiuserdata`、`/userdata/cwaiuserdata` 或升级前备份。

## SSH 安装路径

除了 Web 升级，包内 `scripts/install.sh` 还提供从 main 版本迁移及后续兼容安装的
SSH 入口。它会安装应用、替换并启用 `cosmo.service`，然后由重启启动服务：

```bash
scp build_output/public-runtime/<chip>/<安装包>.tar.gz root@<设备IP>:/tmp/
ssh root@<设备IP>
cd /tmp
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf <安装包>.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
sudo ./scripts/install.sh
sudo reboot
```

该路径假设 Sophon Linux 基础系统和运行依赖已经准备好，不是任意空白硬件的操作系统
镜像安装流程。安装前记录当前版本和恢复方案；安装器会替换当前应用树。

## systemd 服务

已配置设备的服务启动命令为：

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

`scripts/install.sh` 负责升级事务，不创建空白设备的 systemd unit。服务以 `root`
运行并使用 `Restart=on-failure`。

部分 Sophon 系统会在启动时把持久化数据树的属主恢复为设备管理账户。上传暂存服务允许 `sessions` 目录继承一个不可被 group/other 写入的直接父目录属主，同时继续要求：

- `sessions` 是真实目录而不是符号链接，且权限为 `0700`；
- 运行期间属主、设备号和 inode 不变；
- 每个会话目录和载荷仍由当前服务账户创建并保持私有权限。

不要对整个 `<DATADIR>` 做宽泛的递归 `chmod` 或 `chown`。

## 接口文档静态链接

打包接口文件：

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

运行时会链接到：

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`
