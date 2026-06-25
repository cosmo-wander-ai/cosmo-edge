---
title: HTTP Webhook 参考
description: 当前 HTTP 事件推送参数、事件负载字段、属性对象和接收端建议。
prev:
  text: MQTT 接入参考
  link: /reference/mqtt
next:
  text: 模型与资源
  link: /reference/models
---

# HTTP Webhook 参考

CosmoEdge 支持将告警/事件通过 HTTP 推送到外部平台。当前推送配置通过系统接口维护，事件负载字段来自当前事件 DTO 和打包 HTML 接口文档。

## 配置接口

查询：

```text
/gtw/cwai/System/QueryHttpInterfaceParam
```

设置：

```text
/gtw/cwai/System/SetHttpInterfaceParam
```

设置请求示例：

```json
{
  "switch": true,
  "url": "http://example.com/cosmo/events"
}
```

查询响应示例：

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "enable": true,
    "switch": true,
    "url": "http://example.com/cosmo/events"
  }
}
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `switch` | boolean | 是否启用 HTTP 推送 |
| `enable` | boolean | 是否启用 HTTP 推送，查询响应兼容字段 |
| `url` | string | 接收端 URL |

## 事件负载

典型事件负载：

```json
{
  "messageId": "MSG-001",
  "devId": "DEVICE_SN",
  "taskId": "TASK_ID",
  "videoChannelId": "CHANNEL_ID",
  "channelName": "Entrance Camera",
  "timestamp": "1792147200000",
  "algorithmId": "ALG_ID",
  "algorithmCode": "helmet",
  "algorithmName": "Helmet Detection",
  "areaId": "AREA_ID",
  "areaName": "Work Zone",
  "orignalPicture": "/data/event/original.jpg",
  "fullPicture": "/data/event/full.jpg",
  "detectedPicture": "/data/event/target.jpg",
  "video": "/data/event/alarm.mp4",
  "videostructured": "/data/event/structured.json",
  "overviewFile": "/data/event/overview.json",
  "recordId": "RECORD_ID",
  "isRetryMessage": false,
  "category": "alarm",
  "property": {}
}
```

> 事件 DTO（`CMsgOnEventsReq`）中还定义了 `itimestamp`（数值时间戳）和 `files`（关联文件列表），但当前出站序列化（`to_json`）不输出这两个字段，故实际推送负载中不会出现。字段名 `orignalPicture` 沿用当前实现（legacy 拼写）。

字段说明见[字段级 API 参考](api-fields.md#事件上报负载)。

## 属性对象

`property` 会随算法类型（`OnEventsPropertyType`）变化。主类型及其输出键：

| 类别 | 说明 |
| --- | --- |
| `face` | 人脸质量、年龄、性别、口罩、眼镜、特征文件和人脸图 |
| `body` | 人体属性、人体特征和人体图片（`Body` 与 `BodyFeature` 都输出此键） |
| `vehicle` | 车牌、车身颜色、车辆类型、方向和车辆属性 |
| `behavior` | 行为计数、持续时间和目标 ID |
| `machineMaterial` | 物料/设备状态匹配结果 |
| `people` | 人流统计 |
| `car` | 车流统计 |
| `workClothesRecognition` | 工服识别匹配结果 |
| `personCount` | 区域人数统计（同时输出 `persons` 列表） |
| `countNumber` | 计数类事件 |

附加子对象（不是独立的属性类别，而是随主类型一起出现）：

| 子对象 | 出现条件 | 说明 |
| --- | --- | --- |
| `recognition` | `face` 类型同时输出 | 人脸库匹配结果 |
| `persons` | `personCount` 类型同时输出 | 人员目标列表 |
| `target` | 任意类型，当存在目标进出区域信息时附加 | 目标进出区域时间和图片 |

## 接收端建议

- 接收端应返回 `2xx` 表示处理成功。
- 以 `messageId` 或 `recordId` 做幂等处理。
- 对图片、视频和结构化文件 URL 做延迟拉取和失败重试。
- 不要假设 `property` 中所有字段都存在；不同算法只会填充相关字段。
- 当前实现保留历史字段名，例如 `orignalPicture`，接收端需要兼容。

## 打包 HTML 参考

当前仓库保留：

```text
data/Interface/ai-box-interface_v1.0.html
```

安装后入口：

```text
web/staticfile/httpInterface.html
```

该 HTML 可作为历史接口对照，但开源文档应优先以当前 DTO 和 Markdown 参考为准。
