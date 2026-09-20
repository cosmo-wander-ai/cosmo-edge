---
title: MQTT 接入参考
description: 当前 MQTT topic、外层消息结构、注册、心跳、下发请求和响应格式说明。
prev:
  text: 字段级 API 参考
  link: /reference/api-fields
next:
  text: HTTP Webhook 参考
  link: /reference/webhook
---

# MQTT 接入参考

当前 MQTT 实现位于：

```text
src/network/mqtt
src/service/network/impl/MqttLifecycleServiceImpl.cc
```

运行时打包的 HTML 参考位于：

```text
data/Interface/mqtt_v1.0.html
```

## Topic

| 方向 | Topic | 说明 |
| --- | --- | --- |
| 设备 -> 平台 | `/d2p/aibox` | 注册消息和普通响应 |
| 设备 -> 平台 | `/d2p/aibox/heartbeat` | 心跳消息 |
| 平台 -> 设备 | `/p2d/aibox/{deviceSn}` | 平台下发业务请求 |
| 平台 -> 设备 | `/p2d/aibox/heartbeat/{deviceSn}` | 平台心跳相关下发 |

设备连接后会先订阅平台到设备的两个 topic，然后向 `/d2p/aibox` 发送注册消息。注册成功后每 30 秒向 `/d2p/aibox/heartbeat` 发送一次心跳。

## 连接参数

系统配置字段见[字段级 API 参考](api-fields.md#mqtt-参数)。

当前实现支持两种认证模式：

| `authMode` | 行为 |
| --- | --- |
| `0` | 内置 IoT 认证。`clientId` 使用设备 SN，`username` 为 `aibox::{deviceSn}`，`authKey` 为 `cwai` |
| 非 `0` | 普通用户名密码认证。使用配置中的 `clientId`、`userName`、`passwd` |

连接参数中的 `deviceType` 固定为 `box`。

## 外层消息结构

MQTT 外层消息由 `head` 和 `body` 组成：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "request"
  },
  "body": "{\"example\":\"business json string\"}"
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `head.requestId` | string | 请求 ID。响应会复用该值 |
| `head.action` | string | 业务接口路径，例如 `/gtw/cwai/System/QueryDeviceInfo` |
| `head.deviceSn` | string | 设备 SN。设备会拒绝 SN 不匹配的消息 |
| `head.msgType` | string | `register`、`heartbeat`、`request` 或 `response` |
| `body` | string / object | 注册和心跳使用对象；业务请求和响应使用 JSON 字符串，需将业务 JSON 经 `JSON.stringify` 后放入该字段 |

## 注册消息

设备向 `/d2p/aibox` 发送：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "",
    "deviceSn": "DEVICE_SN",
    "msgType": "register"
  },
  "body": {
    "devId": "DEVICE_SN",
    "supplier": "CWAI",
    "aiHostVersion": "v1.0.0",
    "engineType": "cpu",
    "deviceModel": "model",
    "devType": 2
  }
}
```

| body 字段 | 类型 | 是否必须 | 示例 | 说明 |
| --- | --- | --- | --- | --- |
| `devId` | string | 是 | `ai-123123123123` | 设备 ID，通常与设备 SN 一致。 |
| `supplier` | string | 否 | `CWAI` | 供应商标识。 |
| `aiHostVersion` | string | 否 | `1.0.0` | AI 核心版本号。 |
| `engineType` | string | 否 | `CWNN` | 算法引擎类型。 |
| `deviceModel` | string | 否 | `CWAI-AIBOX` | 设备型号。 |
| `devType` | number | 否 | `2` | 设备类型，按平台枚举填写。 |

### 平台到设备

topic：`/p2d/aibox/{设备SN}`

```json
{
  "head": {"requestId":"b5e5035f-9b1e-4578-b3ac-1bed11272c49","action":"","deviceSn":"ai-123123123123","msgType":"register"},
  "body": {"resCode":1,"resMsg":[],"resData":{}}
}
```

| body 字段 | 类型 | 是否必须 | 示例 | 说明 |
| --- | --- | --- | --- | --- |
| `resCode` | number | 是 | `1` | 注册结果：1 成功，0 失败。 |
| `resMsg` | object[] | 否 | `[]` | 返回消息列表。 |
| `resData` | object | 否 | `{}` | 返回数据，注册响应通常为空对象。 |

## 心跳消息

设备向 `/d2p/aibox/heartbeat` 发送：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "",
    "deviceSn": "DEVICE_SN",
    "msgType": "heartbeat"
  },
  "body": {
    "devId": "DEVICE_SN",
    "hostStatus": 0,
    "customScore": 1,
    "cpuUsage": 12.3,
    "memTotal": 8192,
    "memAvailable": 4096,
    "gpuUsage": 0,
    "gpuMemTotal": 0,
    "gpuMemAvailable": 0,
    "diskTotal": 128000,
    "diskAvailable": 64000
  }
}
```

心跳发布 ACK 超时时间为 2000 ms。连续失败达到阈值后会触发重连。

| body 字段 | 类型 | 是否必须 | 示例 | 说明 |
| --- | --- | --- | --- | --- |
| `devId` | string | 是 | `ai-123123123123` | 设备 ID，通常与设备 SN 一致。 |
| `hostStatus` | number | 否 | `0` | 设备或 AI 核心运行状态，按设备枚举上报。 |
| `customScore` | number | 否 | `70` | 设备自定义健康分。 |
| `cpuUsage` | number | 否 | `0.8105` | CPU 使用率。 |
| `memTotal` | number | 否 | `1024` | 内存总量。 |
| `memAvailable` | number | 否 | `1024` | 可用内存。 |
| `gpuUsage` | number | 否 | `0.8105` | GPU 使用率。 |
| `gpuMemTotal` | number | 否 | `1024` | GPU 显存总量。 |
| `gpuMemAvailable` | number | 否 | `501` | GPU 可用显存。 |
| `diskTotal` | number | 否 | `10240` | 磁盘总量。 |
| `diskAvailable` | number | 否 | `1024` | 可用磁盘空间。 |

### 平台到设备

topic：`/p2d/aibox/heartbeat/{设备SN}`

```json
{
  "head": {"requestId":"84d92164-3f32-4c91-8c03-69b41f7b21db","action":"","deviceSn":"ai-123123123123","msgType":"heartbeat"},
  "body": {"resCode":1}
}
```

| body 字段 | 类型 | 是否必须 | 示例 | 说明 |
| --- | --- | --- | --- | --- |
| `resCode` | number | 是 | `1` | 心跳接收结果：1 成功，0 失败。 |

## 平台下发请求

平台向 `/p2d/aibox/{deviceSn}` 下发：

```json
{
  "head": {
    "requestId": "REQ-001",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "request"
  },
  "body": "{}"
}
```

设备收到后会：

1. 校验 `deviceSn` 是否等于本机 SN。
2. 校验 `msgType` 是否为 `request`。
3. 使用 `head.action` 作为 API 路由。
4. 将 `body` 字符串作为业务 JSON 分发到 `ApiRouter`。
5. 通过 `/d2p/aibox` 返回响应。

## 设备响应

设备响应外层结构：

```json
{
  "head": {
    "requestId": "REQ-001",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "response"
  },
  "body": "{\"resCode\":1,\"resMsg\":[],\"resData\":{}}"
}
```

`body` 是业务响应 JSON 字符串，字段结构与 HTTP API 响应保持一致。

## 投递与兼容说明

注册发布 ACK 超时为 5000 ms；连接仍启用时，失败后等待 10 秒重试。心跳间隔为 30 秒，发布 ACK 超时为 2000 ms；连续失败 5 次触发重连。

注册和心跳的 `body` 是对象，业务请求和响应的 `body` 是 JSON 字符串。当前设备上报的 `msgType` 分别为 `register`、`heartbeat`（打包 HTML 上行示例中的 `response` 已过时）。注册和心跳是否成功以 QoS 1 发布确认结果为依据，不依赖平台业务响应中的 `resCode`；上面保留平台响应格式供对接兼容。实现见 `MqttLifecycleServiceImpl.cc` 的 `Register()` 和 `HeartBeat()`。

## 常用业务接口说明

以下 29 项覆盖打包 HTML 中的业务接口章节。`Path` 对应 MQTT 的 `head.action`；`Method: POST` 表示对应 HTTP 调用方式，不是 MQTT 操作方法。**B07 必须走 HTTP，不能通过 MQTT 调用。** 响应示例展示主要字段，通用响应元数据见[字段级 API 参考](api-fields.md)。

本节中的“发送消息”展示的是 MQTT 外层 `body` 字符串内部的业务 JSON。实际 MQTT 下发时，请将该业务 JSON 执行 `JSON.stringify` 后放入外层 `body` 字段。

通道相关接口当前使用 `channelName`、`url`、`videoChannelId`。旧字段 `cameraName`、`rtspUrl`、`cameraId` 不会被当前 DTO 正确接收。

`Camera/Add` 成功响应中的 `resData.id` 就是通道 ID，后续删除、更新、抓图和任务配置接口分别填入 `videoChannelId` 或 `channelId`。

### B01-通道创建

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/Add` |
| Method | POST |
| 接口描述 | 创建实时流、点播、ONVIF、本地视频或 USB 通道。实时 RTSP 接入时必须填写 `channelName`、`url`、`channelType`。 |

发送消息

```json
{
  "channelType": 0,
  "channelName": "测试实时通道",
  "url": "rtsp://:@:554/h264/ch1/main/av_stream",
  "channelCode": "",
  "channelPic": ""
}
```

完整 MQTT 下发示例

```json
{
  "head": {
    "requestId": "7b1d570f-eb9a-4668-91ff-cd04b939eab9",
    "action": "/gtw/cwai/Camera/Add",
    "deviceSn": "",
    "msgType": "request"
  },
  "body": "{\"channelType\":0,\"channelName\":\"测试实时通道\",\"url\":\"rtsp://:@:554/h264/ch1/main/av_stream\",\"channelCode\":\"\",\"channelPic\":\"\"}"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"id": "RT0000000002"}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelType` | number | 是 | `0` | 通道类型：0 直播通道；1 点播通道；2 ONVIF；3 本地视频；6 USB。 |
| `channelName` | string | 建议必填 | `测试实时通道` | 通道名称，页面展示用。 |
| `url` | string | 实时流必填 | `rtsp://...` | 视频流地址。旧字段 `rtspUrl` 无效。 |
| `channelCode` | string | 否 | `CAM-001` | 外部通道号，可为空。 |
| `channelPic` | string | 否 | `/data/pic/1.jpg` | 通道封面或抓图路径，可为空。 |

| 返回字段 | 类型 | 说明 |
| --- | --- | --- |
| `resCode` | number | 1 成功，0 失败。 |
| `resMsg[].msgCode` | string | 业务消息编码。 |
| `resMsg[].msgText` | string | 业务消息文本。 |
| `resData.id` | string | 新增通道 ID，例如 `RT0000000002`。后续接口使用该值。 |

### B02-通道删除

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/Delete` |
| Method | POST |
| 接口描述 | 删除单个视频通道。这里填写的是 `videoChannelId`，不是 `cameraId`。 |

发送消息

```json
{
  "videoChannelId": "RT0000000002"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | 是 | `RT0000000002` | 通道 ID，取自创建通道响应 `resData.id` 或通道列表返回。 |

### B03-通道更新

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/Update` |
| Method | POST |
| 接口描述 | 更新已有通道的名称、流地址、类型等信息。 |

发送消息

```json
{
  "videoChannelId": "RT0000000002",
  "channelType": 0,
  "channelName": "测试实时通道-修改",
  "url": "rtsp://:@:554/h264/ch1/main/av_stream",
  "channelCode": "CAM-001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | 是 | `RT0000000002` | 需要更新的通道 ID。 |
| `channelType` | number | 是 | `0` | 通道类型，枚举同通道创建。 |
| `channelName` | string | 否 | `测试实时通道-修改` | 通道名称。 |
| `url` | string | 否 | `rtsp://...` | 视频流地址。 |
| `channelCode` | string | 否 | `CAM-001` | 外部通道号。 |

### B04-通道分页查询

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/Page` |
| Method | POST |
| 接口描述 | 分页查询视频通道列表，可按名称和状态过滤。 |

发送消息

```json
{
  "pageNum": 1,
  "pageSize": 10,
  "channelName": "测试",
  "channelStatus": 1
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {
    "total": 1,
    "list": [{"videoChannelId": "RT0000000002", "channelName": "测试实时通道", "url": "rtsp://..."}]
  }
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `pageNum` | number | 是 | `1` | 页码，从 1 开始。 |
| `pageSize` | number | 是 | `10` | 每页数量。 |
| `channelName` | string | 否 | `测试` | 按通道名称模糊查询。 |
| `channelStatus` | number | 否 | `1` | 通道状态过滤，按设备实际返回枚举使用。 |

### B05-通道批量删除

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/BatchDelete` |
| Method | POST |
| 接口描述 | 批量删除多个视频通道。 |

发送消息

```json
{
  "videoChannelIds": ["RT0000000002", "RT0000000003"]
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `videoChannelIds` | string[] | 是 | `["RT0000000002"]` | 需要删除的通道 ID 数组。 |

### B06-通道抓图

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Camera/GetPicture` |
| Method | POST |
| 接口描述 | 对指定通道进行抓图。 |

发送消息

```json
{
  "videoChannelId": "RT0000000002"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"picUrl": "/data/picture/RT0000000002.jpg"}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | 是 | `RT0000000002` | 需要抓图的通道 ID。 |

### B07-本地视频通道创建

`POST /gtw/cwai/Camera/AddVideo`

该接口当前要求通过已认证的 HTTP 调用，并使用属于该 HTTP 用户的上传会话。`MessageCameraHandler::Handle(..., context, ...)` 会拒绝 MQTT 调用；打包 HTML 中直接提供设备本地 `filePath` 的旧示例已不适用。

先通过 HTTP 分片上传接口以 `purpose: "video"` 上传视频，再由同一 HTTP 用户提交完成的 `uploadId`。参见[字段级 API 参考：分片上传字段](api-fields.md#分片上传字段)及 [API 概览](api.md)。

HTTP 请求体：

```json
{"uploadId":"COMPLETED_UPLOAD_ID","channelName":"离线视频"}
```

| 字段 | 类型 | 是否必须 | 说明 |
| --- | --- | --- | --- |
| `uploadId` | string | 此流程必填 | 当前 HTTP 用户拥有的、已完成的视频上传会话。 |
| `channelName` | string | 建议必填 | 新通道的显示名称。 |
| `channelCode` | string | 否 | 外部通道号。 |

`contentLength`、`fileName` 和实际本地路径由服务端从上传文件解析；不要传入任意服务器路径。成功响应返回通道 ID：

```json
{"resCode":1,"resMsg":[],"resData":{"id":"VD0000000001"}}
```

### C01-保存任务配置并启用

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/SaveOrUpdate` |
| Method | POST |
| 接口描述 | 给指定通道保存算法任务配置，并启用或更新该任务。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001",
  "scheduleId": "",
  "taskConfig": {
    "params": [{"key": "alarmInterval", "value": "60"}],
    "areas": [],
    "shieldedAreas": [],
    "facesetConfig": []
  }
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID，取自通道创建或列表查询。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID，取自算法列表或通道可配算法接口。 |
| `scheduleId` | string | 否 | — | 时间模板 ID，不使用模板时传空字符串或不传。 |
| `taskConfig` | object | 是 | `{"params":[]}` | 算法运行配置，结构由算法元数据决定。 |
| `taskConfig.params` | object[] | 否 | `[{"key":"alarmInterval","value":"60"}]` | 算法参数列表。 |
| `taskConfig.areas` | object[] | 否 | `[]` | 检测区域列表。 |
| `taskConfig.shieldedAreas` | object[] | 否 | `[]` | 屏蔽区域列表。 |
| `taskConfig.facesetConfig` | object[] | 否 | `[]` | 人脸库相关配置。 |

### C02-修改任务参数

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/ModifyParam` |
| Method | POST |
| 接口描述 | 修改已配置任务的算法参数。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001",
  "taskConfig": {
    "params": [{"key": "alarmInterval", "value": "30"}],
    "areas": [],
    "shieldedAreas": [],
    "facesetConfig": []
  }
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |
| `taskConfig` | object | 是 | `{"params":[]}` | 新的任务配置。 |

### C03-查询任务参数

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/QueryParam` |
| Method | POST |
| 接口描述 | 查询指定通道指定算法的当前任务配置。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"taskConfig": {"params": [], "areas": [], "shieldedAreas": [], "facesetConfig": []}}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |

### C04-启停任务

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/SwitchTask` |
| Method | POST |
| 接口描述 | 启用或停止指定通道上的指定算法任务。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001",
  "switch": 1
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |
| `switch` | number | 是 | `1` | 1 启用，0 停止。 |

### C05-删除任务配置

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/Delete` |
| Method | POST |
| 接口描述 | 删除指定通道和算法的任务配置。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |

### C06-查询通道可配算法

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/SelectAllAlgorithmInfo` |
| Method | POST |
| 接口描述 | 查询指定通道可以配置的算法列表。 |

发送消息

```json
{
  "channelId": "RT0000000002"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": [{"algorithmId": "ALG0000000001", "algorithmName": "安全帽检测"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |

### C07-查询指定算法配置

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Task/SelectConfigByAlgorithmId` |
| Method | POST |
| 接口描述 | 查询指定通道、指定算法的配置详情。 |

发送消息

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"algorithmId": "ALG0000000001", "taskConfig": {"params": []}}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `channelId` | string | 是 | `RT0000000002` | 通道 ID。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |

### D01-创建核心视频任务

| 字段 | 值 |
| --- | --- |
| Path | `/v1/cwai/aihost/TaskCreate` |
| Method | POST |
| 接口描述 | 向 AI 核心创建视频分析任务，通常由平台或业务层根据通道和算法配置下发。 |

发送消息

```json
{
  "taskId": "task-RT0000000002-ALG0000000001",
  "videoChannelId": "RT0000000002",
  "videoChannelName": "测试实时通道",
  "streamUrl": "rtsp://:@:554/h264/ch1/main/av_stream",
  "algorithmCode": "helmet_detect",
  "algorithmUpdateTime": "1716172800000",
  "algorithmId": "ALG0000000001",
  "algorithmName": "安全帽检测",
  "taskConfig": {"params": [], "areas": [], "shieldedAreas": [], "facesetConfig": []}
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `taskId` | string | 是 | `task-RT0000000002-ALG0000000001` | 任务唯一 ID。 |
| `videoChannelId` | string | 是 | `RT0000000002` | 视频通道 ID。 |
| `videoChannelName` | string | 否 | `测试实时通道` | 视频通道名称。 |
| `streamUrl` | string | 否 | `rtsp://...` | 视频流地址。 |
| `algorithmCode` | string | 是 | `helmet_detect` | 算法编码。 |
| `algorithmUpdateTime` | string | 是 | `1716172800000` | 算法更新时间或版本时间戳，按算法包元数据填写。 |
| `algorithmId` | string | 否 | `ALG0000000001` | 平台算法 ID。 |
| `algorithmName` | string | 否 | `安全帽检测` | 算法名称。 |
| `taskConfig` | object | 否 | `{"params":[]}` | 算法运行配置。 |

### D02-取消核心视频任务

| 字段 | 值 |
| --- | --- |
| Path | `/v1/cwai/aihost/TaskCancle` |
| Method | POST |
| 接口描述 | 取消指定核心视频任务。 |

发送消息

```json
{
  "taskId": "task-RT0000000002-ALG0000000001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `taskId` | string | 是 | `task-RT0000000002-ALG0000000001` | 需要取消的任务 ID。 |

### D03-查询核心信息

| 字段 | 值 |
| --- | --- |
| Path | `/v1/cwai/aihost/Info` |
| Method | POST |
| 接口描述 | 查询 AI 核心运行信息。 |

发送消息

```json
{
  "devId": ""
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"devId": "", "hostStatus": 0}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `devId` | string | 是 | — | 设备 SN 或设备 ID。 |

### E01-查询 MQTT 参数

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/System/QueryMqttAdapterParam` |
| Method | POST |
| 接口描述 | 查询设备 MQTT 适配参数。 |

发送消息

```json
{}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"switch": true, "url": "192.168.0.10", "port": 1883, "authMode": 0}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| 无请求参数，发送空对象 `{}`。 |

### E02-设置 MQTT 参数

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/System/SetMqttAdapterParam` |
| Method | POST |
| 接口描述 | 设置设备 MQTT 连接参数。 |

发送消息

```json
{
  "switch": true,
  "url": "192.168.0.10",
  "port": 1883,
  "authMode": 0,
  "clientId": "",
  "userName": "aibox::",
  "passwd": "******"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `switch` | boolean | 是 | `true` | 是否启用 MQTT 适配。 |
| `url` | string | 是 | `192.168.0.10` | MQTT Broker 地址。 |
| `port` | number | 是 | `1883` | MQTT Broker 端口。 |
| `authMode` | number | 是 | `0` | 认证模式，按设备配置枚举填写。 |
| `clientId` | string | 否 | — | 客户端 ID。 |
| `userName` | string | 否 | `aibox::` | MQTT 用户名。 |
| `passwd` | string | 否 | `******` | MQTT 密码。 |

### E03-查询/修改运行模式

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/System/QueryRunModeParam`、`/gtw/cwai/System/ModifyRunModeParam` |
| Method | POST |
| 接口描述 | 查询或修改设备运行模式。 |

查询发送消息

```json
{}
```

修改发送消息

```json
{
  "runMode": 1
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `runMode` | number | 修改时必填 | `1` | 运行模式值，按设备支持的枚举填写。 |

### E04-查询/修改 IoT 网络参数

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/System/QueryIotNetworkParam`、`/gtw/cwai/System/ModifyIotNetworkParam` |
| Method | POST |
| 接口描述 | 查询或修改 IoT 网络参数。 |

查询发送消息

```json
{}
```

修改发送消息

```json
{
  "mqttIp": "192.168.0.10",
  "mqttPort": 1883,
  "httpUrl": "http://192.168.0.10:18080",
  "status": true
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `mqttIp` | string | 修改时必填 | `192.168.0.10` | MQTT 服务地址。 |
| `mqttPort` | number | 修改时必填 | `1883` | MQTT 服务端口。 |
| `httpUrl` | string | 修改时必填 | `http://192.168.0.10:18080` | HTTP 服务地址。 |
| `status` | boolean | 修改时必填 | `true` | 是否启用该网络配置。 |

### F01-算法分页查询

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Page` |
| Method | POST |
| 接口描述 | 分页查询算法列表。 |

发送消息

```json
{
  "pageNum": 1,
  "pageSize": 10,
  "algorithmName": "安全帽",
  "algorithmId": ""
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"total": 1, "list": [{"algorithmId": "ALG0000000001", "algorithmName": "安全帽检测"}]}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `pageNum` | number | 是 | `1` | 页码，从 1 开始。 |
| `pageSize` | number | 是 | `10` | 每页数量。 |
| `algorithmName` | string | 否 | `安全帽` | 算法名称过滤。 |
| `algorithmId` | string | 否 | `ALG0000000001` | 算法 ID 过滤。 |

### F02-添加算法配置

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Add` |
| Method | POST |
| 接口描述 | 新增算法配置记录。 |

发送消息

```json
{
  "algorithmCode": "helmet_detect",
  "algorithmName": "安全帽检测",
  "algorithmCategory": 1,
  "algorithmUsage": 0,
  "checkType": 0,
  "remark": "工地安全检测",
  "eventType": "helmet_alarm",
  "filePath": "/data/algorithm/helmet.zip"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"algorithmId": "ALG0000000001"}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `algorithmCode` | string | 否 | `helmet_detect` | 算法编码。 |
| `algorithmName` | string | 是 | `安全帽检测` | 算法名称。 |
| `algorithmCategory` | number | 是 | `1` | 算法分类，按平台枚举填写。 |
| `algorithmUsage` | number | 是 | `0` | 算法用途，按平台枚举填写。 |
| `checkType` | number | 否 | `0` | 检测类型。 |
| `remark` | string | 否 | `工地安全检测` | 备注。 |
| `eventType` | string | 否 | `helmet_alarm` | 事件类型编码。 |
| `filePath` | string | 否 | `/data/algorithm/helmet.zip` | 算法文件路径。 |

### F03-编辑算法配置

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Update` |
| Method | POST |
| 接口描述 | 编辑已有算法配置。 |

发送消息

```json
{
  "algorithmId": "ALG0000000001",
  "algorithmName": "安全帽检测-新版",
  "algorithmCategory": 1,
  "remark": "更新参数"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |
| `algorithmName` | string | 否 | `安全帽检测-新版` | 算法名称。 |
| `algorithmCategory` | number | 否 | `1` | 算法分类。 |
| `remark` | string | 否 | `更新参数` | 备注。 |

### F04-删除算法配置

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Delete` |
| Method | POST |
| 接口描述 | 删除指定算法配置。 |

发送消息

```json
{
  "algorithmId": "ALG0000000001"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |

### F05-保存算法编排

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/save` |
| Method | POST |
| 接口描述 | 保存编排算法版本和元数据。 |

发送消息

```json
{
  "confVersionId": "CONF0000000001",
  "configVersionName": "安全帽编排-v1",
  "algorithmId": "ALG0000000001",
  "algorithmCategory": "1",
  "algorithmUsage": "0",
  "remark": "编排测试",
  "atomicList": "[]",
  "algorithmProcessdata": "{}",
  "algorithmMetadata": "{}",
  "filePath": "/data/algorithm/layout/helmet.json"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `confVersionId` | string | 是 | `CONF0000000001` | 编排配置版本 ID。 |
| `configVersionName` | string | 否 | `安全帽编排-v1` | 编排配置版本名称。 |
| `algorithmId` | string | 是 | `ALG0000000001` | 算法 ID。 |
| `algorithmCategory` | string | 否 | `1` | 算法分类。 |
| `algorithmUsage` | string | 否 | `0` | 算法用途。 |
| `remark` | string | 否 | `编排测试` | 备注。 |
| `atomicList` | string | 否 | `[]` | 原子能力列表，当前 DTO 为字符串，传 JSON 字符串。 |
| `algorithmProcessdata` | string | 否 | `{}` | 算法流程数据，当前 DTO 为字符串，传 JSON 字符串。 |
| `algorithmMetadata` | string | 否 | `{}` | 算法元数据，当前 DTO 为字符串，传 JSON 字符串。 |
| `filePath` | string | 否 | `/data/algorithm/layout/helmet.json` | 编排文件路径。 |

### F06-查询编排算法列表

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/list` |
| Method | POST |
| 接口描述 | 查询编排算法列表。 |

发送消息

```json
{
  "supplier": "CWAI",
  "algorithmUsage": -1,
  "filePath": ""
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": []
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `supplier` | string | 否 | `CWAI` | 供应商标识。 |
| `algorithmUsage` | number | 否 | `-1` | 算法用途过滤，-1 表示不过滤。 |
| `filePath` | string | 否 | — | 文件路径过滤。 |

### F07-查询编排算法详情

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/detail` |
| Method | POST |
| 接口描述 | 查询指定编排算法详情。 |

发送消息

```json
{
  "id": "ALG0000000001",
  "filePath": "/data/algorithm/layout/helmet.json"
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"id": "ALG0000000001"}
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `id` | string | 是 | `ALG0000000001` | 编排算法 ID。 |
| `filePath` | string | 否 | `/data/algorithm/layout/helmet.json` | 编排文件路径。 |

### F08-查询原子动作列表

| 字段 | 值 |
| --- | --- |
| Path | `/gtw/cwai/atomic/action/list` |
| Method | POST |
| 接口描述 | 查询原子动作列表。 |

发送消息

```json
{
  "actionUsage": 0,
  "filePath": ""
}
```

响应消息

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": []
}
```

| 请求字段 | 类型 | 是否必须 | 示例 | 备注 |
| --- | --- | --- | --- | --- |
| `actionUsage` | number | 否 | `0` | 动作用途，按平台枚举填写。 |
| `filePath` | string | 否 | — | 动作定义文件路径。 |

## 常用业务 action

| 模块 | action 示例 |
| --- | --- |
| 核心任务 | `/v1/cwai/aihost/TaskCreate`、`/v1/cwai/aihost/TaskCancle`、`/v1/cwai/aihost/Info` |
| 系统 | `/gtw/cwai/System/QueryDeviceInfo`、`/gtw/cwai/System/QueryHardwareResource`、`/gtw/cwai/System/QueryMqttAdapterParam`、`/gtw/cwai/System/SetMqttAdapterParam` |
| 任务配置 | `/gtw/cwai/Task/ModifyParam`、`/gtw/cwai/Task/QueryParam`、`/gtw/cwai/Task/SwitchTask`、`/gtw/cwai/Task/SaveOrUpdate`、`/gtw/cwai/Task/Delete` |
| 摄像机 | `/gtw/cwai/Camera/Add`、`/gtw/cwai/Camera/Update`、`/gtw/cwai/Camera/Page`、`/gtw/cwai/Camera/Delete`、`/gtw/cwai/Camera/GetPicture` |
| 算法 | `/gtw/cwai/Algorithm/Page`、`/gtw/cwai/Algorithm/Add`、`/gtw/cwai/Algorithm/Update`、`/gtw/cwai/Algorithm/Delete`、`/gtw/cwai/algorithm/layout/save` |
