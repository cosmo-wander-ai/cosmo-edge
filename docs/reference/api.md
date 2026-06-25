---
title: API 概览
description: 当前 HTTP、MQTT-facing API 类别、路由入口和打包接口文档入口。
prev:
  text: 架构概览
  link: /guide/architecture
next:
  text: 字段级 API 参考
  link: /reference/api-fields
---

# API 概览

本文只记录当前源码中可以确认的 API 类别和入口。字段级接口说明请继续阅读[字段级 API 参考](api-fields.md)、[MQTT 接入参考](mqtt.md)和[HTTP Webhook 参考](webhook.md)。

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

该组路由标记为 `kNoAuth`（免鉴权），注册在 `src/api/ApiRouter.cc` 的 `RegisterCoreRoutes()`。`/v1/cwai/aihost/` 下共 19 个端点：

```text
InterfaceTest             TaskCreate                TaskCancle
PTaskCreate               PTaskCancle               PTaskDetectPic
OperateNode               Info                      Probe
ViewRoutes                GraphicsMemory            OverviewStructrueRecord
LoadLocalAlgorithmAction  LogicTest                 QueryTaskOverviewFile
QueryTaskStatus           QueryTaskInfo             QueryDeviceMemStatus
QueryLogs
```

另外为前端统一前缀提供了 3 个 `/gtw/cwai/aihost/` 兼容路由：`PTaskCreate`、`PTaskCancle`、`PTaskDetectPic`（同样 `kNoAuth`）。

## API 类别

| 类别 | 路由前缀 | 说明 |
| --- | --- | --- |
| 登录 | `/gtw/cwai/login/` | 登录和密码修改 |
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

路由注册中存在 `kAuth` 和 `kNoAuth` 两类标记。HTTP 请求会校验 `mtk` token；MQTT 下发请求通过 `ApiRouter` 分发时使用空 `mtk`，不会走 HTTP token 校验。

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
