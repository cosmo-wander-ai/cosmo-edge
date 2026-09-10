---
title: API 概览
description: 当前 HTTP、MQTT-facing API 类别、路由入口和打包接口文档入口。
prev:
  text: 架构概览
  link: /guide/architecture
next:
  text: 图片检测 API 接入指南
  link: /reference/image-detection-api
---

# API 概览

本文只记录当前源码中可以确认的 API 类别和入口。图片上传与同步推理的完整调用流程请阅读[图片检测 API 接入指南](image-detection-api.md)；其他字段级接口说明请继续阅读[字段级 API 参考](api-fields.md)、[MQTT 接入参考](mqtt.md)和[HTTP Webhook 参考](webhook.md)。

## 路由入口

后端 API 路由集中在：

```text
src/api/ApiRouter.cc
src/api/ApiRouterRoutes.cc
```

主要管理端接口位于：

```text
/gtw/cwai/...
```

核心 AI Host 接口位于：

```text
/v1/cwai/aihost/...
/gtw/cwai/aihost/...
```

该组路由注册在 `src/api/ApiRouter.cc` 的 `RegisterCoreRoutes()`。除最小存活检查 `Probe` 为 `kNoAuth` 外，其余端点均为 `kAuth`，HTTP 调用必须携带有效 `mtk`。`/v1/cwai/aihost/` 下共 19 个端点：

```text
InterfaceTest             TaskCreate                TaskCancle
PTaskCreate               PTaskCancle               PTaskDetectPic
OperateNode               Info                      Probe
ViewRoutes                GraphicsMemory            OverviewStructrueRecord
LoadLocalAlgorithmAction  LogicTest                 QueryTaskOverviewFile
QueryTaskStatus           QueryTaskInfo             QueryDeviceMemStatus
QueryLogs
```

另外为前端统一前缀提供了 3 个 `/gtw/cwai/aihost/` 兼容路由：`PTaskCreate`、`PTaskCancle`、`PTaskDetectPic`（均为 `kAuth`）。

## API 类别

| 类别 | 路由前缀 | 说明 |
| --- | --- | --- |
| 登录 | `/gtw/cwai/login/` | 登录免鉴权；密码修改需要 header `mtk`，成功后该用户所有会话失效 |
| 网络 | `/gtw/cwai/network/` | 网卡、DNS、网络质量和连通性检查 |
| 算法 | `/gtw/cwai/Algorithm/` | 算法分页、上传、新增、更新、删除、客流算法列表 |
| 算法编排 | `/gtw/cwai/algorithm/layout/` | 编排算法保存、详情、列表、导出单个算法(`exportSingleAlg`，zip)和导出全部(`export`，tar.gz) |
| 原子动作 | `/gtw/cwai/atomic/action/list` | Pipeline action 列表 |
| 模型管理 | `/gtw/cwai/atomic/Model/` | 模型列表、上传、配置、导入、删除和导出 |
| 计划 | `/gtw/cwai/schedule/` | 计划新增、更新、分页、删除和查询 |
| 事件 | `/gtw/cwai/Event/` | 事件分页、告警导出、客流统计 |
| 摄像头 | `/gtw/cwai/Camera/` | 摄像头增删改查、取图、USB 摄像头列表 |
| 任务 | `/gtw/cwai/Task/` | 参数、区域、策略、开关、批量操作、运行详情 |
| 系统 | `/gtw/cwai/System/` | 设备、时间、画质、录像、升级、Logo、调试、HTTP/MQTT 参数等 |
| 人脸底库 | `/gtw/cwai/Library/` | 人脸库和人员图片管理 |
| 人体底库 | `/gtw/cwai/BodyLibrary/` | 人体特征库管理 |
| 物品底库 | `/gtw/cwai/ThingsLibrary/` | 物品库管理 |
| 文件导入 | `/gtw/cwai/File/` | 导入文件和导入状态 |
| 音频 | `/gtw/cwai/Audio/` | 音频文件、音柱设备和测试 |
| 联动 / 告警策略 | `/gtw/cwai/AlarmStrage/` | 存储策略、增删改查和开关 |
| 实时流 | `/gtw/cwai/LiveStream/` | 请求直播、保活和停止 |

## 认证

路由注册中存在 `kAuth` 和 `kNoAuth` 两类标记。HTTP 请求会校验 `mtk` token；MQTT 只有在内部客户端按配置建立连接并完成设备注册后，才会以受信 transport 上下文进入同一路由器，不重复使用 HTTP `mtk` 校验。

公开 API 文档中仍需要补充：

- 登录接口的请求和响应字段。
- token 的传递位置。
- 默认账号策略。
- token 过期和错误码说明。

## 响应头字段

大多数管理端响应继承 `MsgSendHead`：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `resCode` | number | CWAI 响应码，`1` 表示成功，`0` 表示失败 |
| `resMsg` | object[] | 错误或提示信息列表 |
| `resultCode` | string | ChinaMobile 兼容响应码 |
| `resultMsg` | string | ChinaMobile 兼容响应信息 |

`MsgSendHead` 本身不含业务数据；各具体响应消息（各 `*Send` 子类）会在 `MsgSendHead` 之外额外携带 `resData` 业务数据容器，其结构按接口不同而变化。

## 资源感知传输

上传模型组件、模型归档、本地视频、算法包、升级包、音频、人脸导入包和图片时，客户端应复用同一套鉴权分片协议：

| 接口 | 说明 |
| --- | --- |
| `POST /gtw/cwai/atomic/model/uploadCapabilities` | 查询设备当前能力和安全可用空间 |
| `POST /gtw/cwai/atomic/model/uploadTemp` | 上传一个 `multipart/form-data` 分片 |
| `POST /gtw/cwai/atomic/model/cancelUpload` | 取消会话并立即释放预留空间 |

`uploadCapabilities` 中的数字字节数以十进制字符串返回。主要字段：

| 字段 | 语义 |
| --- | --- |
| `maxTotalSize` | 可选部署策略中的单文件总量上限；`0` 表示不设置产品配额 |
| `maxChunkSize` | 单个传输分片上限；默认 8 MB（8 × 1024 × 1024 bytes） |
| `maxChunks` | 可选部署策略中的分片数上限；`0` 表示不设置产品配额 |
| `availableBytes` | 暂存目标文件系统当前可用字节数，尚未扣除安全储备 |
| `reserveBytes` | 当前生效的磁盘安全储备 |
| `availableForNewUploadsBytes` | 扣除安全储备和在途会话预留后的当前可接纳字节数 |
| `reservedBySessionsBytes` | 已被在途上传会话预留的字节数 |
| `activeSessions` | 当前打开或已完成但尚未消费的上传会话数 |
| `idleTimeoutMs` | 无进展会话的过期时间；每个有效分片都会刷新 |
| `absoluteTimeoutMs` | 绝对生命周期；`0` 表示不设置绝对时长 |
| `resumable` | 是否支持幂等续传 |
| `persistentAcrossRestart` | 是否可在引擎重启后恢复 |
| `maxEncodedImageBytes` | 当前媒体管线可接收的编码图片字节数 |
| `maxImagePixels` | 当前媒体管线可解码的最大像素数 |

默认策略不按模型、视频或图片类型设置任意总量配额。设备按目标文件系统的实时资源决定是否接纳，默认保留固定 `512 MB` 磁盘安全空间，百分比预留默认为 `0%`。客户端必须在开始上传前查询能力，不能把某次查询得到的可用空间或图片能力写死为产品常量。

当前生产默认值集中在一处，而不是按业务类型设置多套细粒度上限：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| 完整文件总量 / 分片数 | `0` / `0` | 不设置产品配额；仍受实时资源接纳结果约束 |
| 单用户 / 全局并发会话数 | `0` / `0` | 不设置固定会话配额 |
| 全局在途预留总量 | `0` | 不设置固定预留配额 |
| 单个上传分片 | `8 MB` | 控制单次请求的内存和解析开销 |
| 单会话元数据预算 | `64 MB` | 防止异常会话消耗无界内存 |
| 空闲超时 / 绝对超时 | `30 分钟` / `0` | 有进展即续期，不设置绝对生命周期 |
| 跨重启恢复 | 启用 | 清单持久化并支持幂等续传 |
| 磁盘安全储备 | `512 MB`（百分比预留默认 `0%`） | 避免上传耗尽目标文件系统；以能力接口返回的 `reserveBytes` 为准 |

首个分片应携带稳定的 `clientRequestId`；服务端返回不透明的 `uploadId` 和 `nextChunkIndex`。断线或引擎重启后，重新发送相同文件的第 0 块和同一 `clientRequestId`，服务端会返回原会话及下一待上传分片。完成或取消后，客户端应删除本地恢复标识。

控制面 JSON 请求默认限制为 1 MB；普通单次 multipart 请求默认限制为 10 MB；推荐上传分片为 8 MB。这里的 MB 均按 1024 × 1024 bytes 计算。它们是单次 HTTP 请求的内存与解析边界，不是业务文件总量限制。超过边界时服务端返回 HTTP 413 和 `HTTP_BODY_TOO_LARGE`，并建议 `USE_CHUNKED_UPLOAD` 或 `REDUCE_REQUEST_BODY`。

### 升级恢复状态

升级请求接受 `uploadId`，其原始文件名必须匹配
`cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz`。后端在重启前校验
文件名、MD5、归档安全和目录结构；重启后由统一启动脚本再次校验 MD5 并安装。
Open 与 Protected 包使用相同升级协议，模型授权不参与应用包校验。

`POST /gtw/cwai/System/QueryDeviceStatus` 成功时返回：

| 字段 | 语义 |
| --- | --- |
| `resData.bootId` | 当前 Linux 启动实例标识；软件升级页面用它确认设备确实完成了一次重启 |
| `resData.softwareVersion` | 当前运行中的 CosmoEdge 版本 |

这两个字段是向后兼容的增量字段。旧客户端可以忽略；新客户端不应只根据“接口再次返回 200”就宣告升级成功。

libevent 另有 12 MB 的有限紧急接收后备线，只在请求未被应用层边界提前拒绝时兜底，不能视为可用的业务上传额度。底层直接拒绝时可能只能返回通用 HTTP 413；Web 控制台会按请求类型补成同样可执行的提示（multipart 改用分片，其他请求缩小请求体）。第三方客户端仍应遵守 8 MB 分片、1 MB JSON 和 10 MB 普通 multipart 边界。

图片 URL 下载使用“当前可用内存、媒体帧能力”共同计算的预算，不再使用固定 16 MB 阈值；视频和其他大文件的 HTTP 获取直接流式写入文件。媒体静态路径按扩展名返回标准 MIME 类型（例如 JPEG 为 `image/jpeg`、MP4 为 `video/mp4`）。模型和其他受管文件的导出同样按文件流发送，并支持单段 `Range` 请求、`206 Partial Content` 和不可满足范围的 `416`。

`/gtw/cwai/atomic/model/exportConfig` 对用户管理且允许导出的模型直接返回附件。预置、加密或与设备绑定的模型会返回 `DefaultCantBeExport`；这是模型可移植性和安全策略边界，不是文件大小配额。

## 可操作错误

`resMsg` 在兼容 `msgCode`、`msgText` 的基础上可携带：

| 字段 | 说明 |
| --- | --- |
| `messageKey` | 前端本地化键 |
| `details` | `actualBytes`、`limitBytes`、`requiredBytes`、`availableBytes`、`reserveBytes` 等机器可读上下文 |
| `retryable` | 原请求在外部条件变化后是否可重试 |
| `retryAfterSeconds` | 建议等待时间 |
| `recommendedAction` | 前端应展示或执行的下一步 |

上传和媒体链路的主要错误包括 `STORAGE_RESERVE_REACHED`、`TRANSFER_BUSY`、`UPLOAD_METADATA_BUDGET`、`HTTP_BODY_TOO_LARGE`、`IMAGE_INPUT_TOO_LARGE` 和 `IMAGE_RESOLUTION_TOO_LARGE`。前端应展示服务端返回的实际值、限制值和建议动作，不能统一降级为“上传失败”。

当前前端会把 `recommendedAction` 映射为可见操作提示。稳定动作码包括 `FREE_DISK_SPACE`、`USE_CHUNKED_UPLOAD`、`REDUCE_REQUEST_BODY`、`USE_LARGER_CHUNKS`、`RETRY`/`RETRY_LATER`、`RESIZE_IMAGE`、`RESIZE_OR_RECOMPRESS_IMAGE`、`CHECK_UPLOAD_PARAMETERS`、`CHANGE_DEPLOYMENT_POLICY` 和 `USE_LARGER_CHUNKS_OR_CHANGE_POLICY`；若同时返回 `retryAfterSeconds`，界面还会显示建议等待时间。

## WebSocket

默认 WebSocket 端口：

```text
9000
```

入口由事件通知器初始化：

```text
InitializeWebSocket("0.0.0.0", kDefaultWebSocketPort)
```

## 打包接口文档

当前仓库仍保留运行时可访问的 HTML 接口文档：

```text
data/Interface/ai-box-interface_v1.0.html
data/Interface/mqtt_v1.0.html
```

安装后会生成静态入口：

```text
web/staticfile/httpInterface.html
web/staticfile/mqttInterface.html
```

系统接口也提供文档 URL 查询：

| 类型 | 返回路径 |
| --- | --- |
| `type = 0` | `/staticfile/httpInterface.html` |
| `type = 1` | `/staticfile/mqttInterface.html` |
