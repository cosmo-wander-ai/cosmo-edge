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

包内 `scripts/install.sh` 由源码中的 `scripts/legacy_migration_install.sh` 打包生成。
先按[构建指南](./build.md)取得与目标一致的产物，再选择下面的部署入口。

| 目标平台 | 默认 Open 取包目录 | 默认持久化数据根 | 部署入口 |
| --- | --- | --- | --- |
| x86 Linux / Windows | `build_output/`（运行时导出） | 容器内 `/data/cwaiuserdata`，挂载命名卷 | 重建对应 Compose 服务 |
| Apple Silicon macOS Preview | `build_output/macos-x86/` | 容器内 `/data/cwaiuserdata`，使用独立命名卷 | Mac Preview 脚本 |
| Sophon BM1688 | `build_output/public-runtime/bm1688/` | `/data/cwaiuserdata` | 本页 SSH 安装或设备 Web 升级 |
| Sophon CV186X | `build_output/public-runtime/cv186x/` | `/data/cwaiuserdata` | 本页 SSH 安装或设备 Web 升级 |
| Rockchip RK3576 | `build_output/rk3576/` | `/userdata/cwaiuserdata` | 本页 SSH 安装 |
| Rockchip RV1126B | `build_output/rv1126b/` | `/userdata/cwaiuserdata` | 本页 SSH 安装 |

Protected 仅支持 BM1688、CV186X 和 RK3576，取包目录均为
`build_output/production-release/<chip>/`；SDK、模型和授权前提见
[高级构建选项](./build.md#高级构建选项)。应用安装目录默认为
`/appfs/cosmo_wander/cwai_data`。按芯片和配置取包，不以文件名相似判断兼容性。
安装方法不代表每个平台的设备验收均已通过，实际范围以对应发布记录为准。

## x86 Docker 运行环境

以下命令在宿主机的仓库根目录执行。首次准备见[构建指南](./build.md#x86-docker-开发运行环境)。

启动：

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

停止：

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

查看日志：

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

更新 Linux/Windows 服务时使用同一工作目录和 Compose 项目重新执行 `up -d --build`；
Mac 使用 `./scripts/macos-docker-preview.sh up --build`。更新前保存用户数据卷和资源卷快照，
更新时保留这两个卷，不运行 `down -v`。已有资源卷不会随新镜像自动更新。

确认容器运行、管理页面可访问、软件版本符合本次构建，且原有用户、任务和模型仍可读取。
容器重建不要求宿主机 `bootId` 变化，容器中也不使用 `cosmo.service`。

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

Web 升级入口及启动升级脚本要求包根目录包含 `bin`、`files`、`font`、`lib`、
`scripts` 和 `web`，其中 `lib` 是必需目录。升级入口的目录校验不强制 `resource`。

正式构建包还经过 `scripts/verify_package_contents.py` 审计：除上述目录外，必须保留
`resource`，并检查可执行文件、运行路径声明、许可证、目标芯片及 Open/Protected 策略。
`COSMO_PACKAGE_MODELS=preserve` 表示不携带模型权重，不表示可以省略正式包要求的目录。

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

下面的安装包、`bootId` 和 systemd 步骤适用于边缘设备。x86 的备份与重建按
[Docker 运行环境](#x86-docker-运行环境)执行，再完成下方的容器与业务检查。

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

**边缘设备：** BM1688 可以使用前述 Web 升级流程或下方 SSH 入口；其他芯片按对应
取包目录使用 SSH 安装。重启后确认 `bootId` 已变化、`cosmo.service` 为 `active`，
软件版本与目标包一致。用 `systemctl status cosmo.service --no-pager` 检查服务。

**x86 Docker：** 保留原用户数据卷和资源卷，通过对应 Compose 入口重建。用该入口的
`ps` 检查容器状态，并确认管理页面响应、软件版本和数据保留；不套用设备的 bootId/systemd 检查。

两种环境都应完成业务检查：

1. 原用户、摄像头、任务、参数、用户模型和事件历史仍可读取；
2. 启动一个真实视频任务，确认推理、预览和至少一个带媒体的告警；
3. 创建 staged upload，会话在服务重启后可恢复，成功消费或取消后可清理；
4. 设备再次重启，或 x86 容器重启后，复查登录、任务、模型、历史和上传状态；
5. 保留升级前后数据清单、服务日志和 PASS/FAIL 结果。

仓库测试 `bash test/test_legacy_migration_installer.sh` 验证数据复制、瞬时目录排除、
幂等、冲突拒绝和事务回滚；它是主机文件系统测试，不能替代最终包在真实设备上的
升级、推理和告警证据。

### 恢复边界

安装器在返回前发生错误时会恢复旧应用；Rockchip 迁移也会保留旧数据根。安装器
返回后，如果新服务无法通过实际业务验收，当前版本**不会**根据 HTTP 健康状态自动
回滚。保持设备供电，先保存 `systemctl status cosmo.service` 和日志，再通过 SSH
重新安装已记录 SHA-256 的旧包，并按升级前备份恢复其数据根。恢复完成前不要删除
`/data/cwaiuserdata`、`/userdata/cwaiuserdata` 或升级前备份。

## 从 1.1.0 升级到 1.2.0

公开 Open 包按 BM1688、CV186X、RK3576 和 RV1126B 分别构建。升级方法适用于与设备
芯片匹配的包；本轮发布仅在 BM1688 测试设备上检查安装、升级和回滚。其他芯片的
构建与包审计结果不能代替设备升级结论，具体结果见对应的发布清单。

升级前取得旧版安装包和数据恢复入口，并完成以下检查：

1. 在对应芯片目录核对 1.2.0 包的 `TARGET_CHIP` 与 `SHA256SUMS`，确认包名中的
   MD5 与归档内容一致。不要跨芯片或跨 Open／Protected 配置使用包。
2. 记录旧版 `bin/version.txt`、服务状态、实际持久化目录和需要保留的配置、
   数据库、用户模型及资源清单。完成或取消正在进行的上传。
3. 停止服务，把持久化数据备份到独立文件系统，并确认备份可以读取；同时保留
   旧版包。自定义模板或模型若与新包使用相同路径，须单独备份。默认资源合并规则
   会让新包文件替换同路径旧文件；`CLEAN_RESOURCE=1` 会仅保留包内资源。

BM1688 可使用本页的 Web 升级流程或下方 SSH 安装入口。升级后核对版本、
`cosmo.service` 状态、管理入口可达性，以及配置、数据库记录和持久化文件是否
按预期保留；再次重启后复查。Web 页面的 15 分钟等待结束不表示设备端安装已经
停止，应先查服务和日志，再决定恢复操作。

若安装返回后新服务无法正常启动，保存故障日志，通过 SSH 重新安装已核验的
旧版包，并恢复与旧版对应的升级前数据快照。恢复后核对旧版版本、服务和数据；
不要假定旧程序能读取 1.2.0 新产生的数据。升级与回滚的实际通过范围须与候选
commit、包 SHA-256 和目标设备绑定记录。

x86 Linux／Windows 更新 Compose 服务时保留用户数据卷和应用资源卷，不运行
`down -v`；旧资源卷不会随镜像自动更新。Apple Silicon macOS Preview 也应先保存
对应卷快照，再更新和恢复其独立 Compose 环境。

## SSH 安装路径

此入口适用于已准备好基础 Linux 系统、systemd、nginx、Python 3、pgrep 和目标平台
运行依赖的 Sophon / Rockchip 设备。Rockchip 的 `/userdata` 必须是可写持久化挂载点。
软件包不安装操作系统或自动补齐设备运行依赖。

安装前记录当前版本、服务状态和恢复方案，备份实际数据根及需要保留的自定义资源。
沿用上方的[升级与恢复边界](#从-1-1-0-升级到-1-2-0)。安装器会替换应用、创建或替换
并启用 `cosmo.service`，安装成功后重启设备。

### 1. 构建主机：核验并传输

以下 Bash 示例选择 **BM1688 Open**。其他目标只将 `package_dir` 改为本页取包表中的
对应目录；将 `device` 替换为实际 SSH 账号和地址。先人工核对 `TARGET_CHIP` 与设备身份，
再传输；不要直接执行未核验的下载包。

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

Windows Sophon 构建先完成[构建指南中的 PowerShell 校验](./build.md#将-sophon-构建包交给部署流程)，
在同一 PowerShell 终端沿用校验成功的 `$outputDir` 和 `$packageName`：

```powershell
$device = 'root@<device_ip>'
scp (Join-Path $outputDir $packageName) (Join-Path $outputDir 'TARGET_CHIP') (Join-Path $outputDir 'SHA256SUMS') "${device}:/tmp/"
if ($LASTEXITCODE -ne 0) { throw 'Package transfer failed' }
```

确认传输成功后，在构建主机连接设备（Bash / PowerShell 均可）：

```sh
ssh "$device"
```

### 2. 设备：安装并重启

以下命令在上一步打开的**设备 root 会话**执行。再次确认 `TARGET_CHIP`；使用随包传输的
清单选取唯一实际文件名，不匹配旧版本通配符。任一步失败都会退出本组操作，不继续重启。

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

设备恢复后重新连接，核对版本和服务，并完成[安装和升级后验收](#安装和升级后验收)。
安装器成功返回只代表文件事务完成；服务或业务验收失败时，按本页恢复步骤处理。

## systemd 服务

已配置设备的服务启动命令为：

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

包内 `scripts/install.sh` 在首次安装和升级时都会创建或替换并启用 `cosmo.service`，
同时安装日志清理服务。应用服务以 `root` 运行，使用 `Restart=on-failure`；安装后由重启启动。
它要求设备基础系统和运行依赖已经就绪，不负责准备空白设备的操作系统。

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
