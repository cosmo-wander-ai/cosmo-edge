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

## 基本信息

| 字段 | 值 |
| --- | --- |
| 请求地址 | 用户在平台页面配置的 URL |
| 请求方法 | POST |
| Content-Type | application/json |
| 成功判定 | HTTP `200`，响应体为合法 JSON。 |

发送端要求 HTTP `200`，且响应体可解码为响应 DTO；空响应体不算成功。当前发送端不额外检查业务 `resCode`，接收端成功时仍应返回 `{"resCode":1,"resMsg":[]}`。

`AlarmPushServiceImpl::OnEvents()` 明确从本地文件转为 base64 的字段只有 `orignalPicture`、`fullPicture`、`detectedPicture`、`property.face.image` 和 `property.recognition.LibImage`；图片不存在或不可读取时可能为空字符串。不要将所有嵌套图片字段都视为 base64：`property.body.image`、`persons` 中的图片及其他算法属性按上游内容透传。视频和结构化文件路径不会自动转换为公开 URL。

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
  "orignalPicture": "/9j/4AAQ...",
  "fullPicture": "/9j/4AAQ...",
  "detectedPicture": "/9j/4AAQ...",
  "video": "/data/event/alarm.mp4",
  "videostructured": "/data/event/structured.json",
  "overviewFile": "/data/event/overview.json",
  "recordId": "RECORD_ID",
  "isRetryMessage": false,
  "category": "alarm",
  "targets": [
    {
      "label": "category0",
      "confidence": 0.93,
      "trackId": "TARGET_TRACK_ID",
      "box": {"x": 120, "y": 80, "width": 240, "height": 360}
    }
  ],
  "property": {}
}
```

> 事件 DTO（`CMsgOnEventsReq`）中还定义了 `itimestamp`（数值时间戳）和 `files`（关联文件列表），但当前出站序列化（`to_json`）不输出这两个字段，故实际推送负载中不会出现。字段名 `orignalPicture` 沿用当前实现（legacy 拼写）。

字段说明见[字段级 API 参考](api-fields.md#事件上报负载)。

## 根字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `messageId` | string | 事件唯一 ID，用于链路追踪和重推去重。 |
| `devId` | string | 设备 ID。 |
| `taskId` | string | 任务 ID。 |
| `videoChannelId` | string | 通道 ID。 |
| `channelName` | string | 通道名称。 |
| `timestamp` | string | 13 位毫秒时间戳。 |
| `algorithmId` | string | 算法 ID。 |
| `algorithmCode` | string | 算法编码。当前实现通常与 `algorithmId` 相同。 |
| `algorithmName` | string | 算法名称。 |
| `areaId` | string | 事件发生区域 ID。 |
| `areaName` | string | 事件发生区域名称。 |
| `orignalPicture` | string | 原始全景图。字段名历史拼写为 `orignalPicture`，保持兼容。 |
| `fullPicture` | string | 带叠加框的全景图。 |
| `detectedPicture` | string | 检测目标小图，可为空。 |
| `video` | string | 告警视频路径，可为空。 |
| `videostructured` | string | 视频对应的目标、区域结构化信息文件路径，可为空。 |
| `overviewFile` | string | 事件过程概览文件路径，可为空。 |
| `recordId` | string | 目标或告警记录 ID。开启跟踪时，同一目标通常保持不变。 |
| `isRetryMessage` | boolean | 是否为离线重推消息。 |
| `category` | string | 算法类别。 |
| `property` | object | 算法扩展属性。仅在对应算法有扩展属性时出现。 |

## 检测目标

`targets` 是触发本次事件的检测目标列表。每个元素包含：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `label` | string | 模型检测标签名称，例如 `category0`、`person` 或 `Pedestrian` |
| `confidence` | number | 检测置信度 |
| `trackId` | string | 目标跟踪 ID；未启用跟踪时可能不输出 |
| `box` | object | 像素坐标目标框，包含 `x`、`y`、`width`、`height` |

无独立目标的统计类事件可能不输出 `targets`。HTTP 首次推送与离线重推使用相同结构。

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

## property 输出规则

`property` 会按算法类型输出对应子对象，不表示所有子对象都会同时出现。

### 人脸检测与识别

```json
{
  "property": {
    "face": {"quality":92.5,"age":25,"gender":0,"wearMask":0,"wearGlasses":0,"featureUrl":"","image":"/9j/4AAQ..."},
    "recognition": {"matchDegree":95.5,"matchLibName":"人脸库","matchId":"001","LibImage":"/9j/4AAQ...","matchName":"张三","personCode":"10002","personId":"10002"}
  }
}
```

### 人体属性或人体特征

```json
{
  "property": {
    "body": {
      "image":"/data/event/body.jpg",
      "topLength":1,
      "topColor":1,
      "bottomLength":1,
      "bottomColor":1,
      "inhand":1,
      "quality":0.98,
      "inAreaTime":"1681123739000",
      "inAreaFullImageUrl":"",
      "outAreaTime":"1681123749000",
      "outAreaFullImageUrl":"",
      "featureUrl":"",
      "feature":""
    }
  }
}
```

### 车辆属性

```json
{
  "property": {
    "vehicle": {
      "plateColor":"0",
      "vehicleColor":"5",
      "vehicleClass":"K33",
      "orientation":"1",
      "plate":"苏A88888",
      "plateSrc":"苏A88888",
      "attrs":[{"category":"vehicleColor","label":"白色","atomicCode":"white","confidence":0.93}]
    }
  }
}
```

### 行为类属性

```json
{"property":{"behavior":{"targetId":"qsddf123","duration":300,"count":10}}}
```

### 机物与工服比对

```json
{
  "property": {
    "machineMaterial": {"runningStatus":"1","matchId":"1","matchDegree":85,"groupId":"group-1","groupName":"物品库","baseImageUrl":"http://example.com/base.jpg"}
  }
}
```

```json
{
  "property": {
    "workClothesRecognition": {"matchId":"1","matchDegree":85,"groupId":"group-1","groupName":"工服库","baseImageUrl":"http://example.com/base.jpg"}
  }
}
```

### 人数、车流、单帧计数与聚众

```json
{"property":{"people":{"enterNumber":20,"leaveNumber":10,"enterOrgNum":3,"leaveOrgNum":1,"time":"20260519101215"}}}
```

```json
{"property":{"car":{"enterNumber":20,"leaveNumber":10,"enterOrgNum":3,"leaveOrgNum":1,"time":"20260519101215"}}}
```

```json
{"property":{"countNumber":20}}
```

```json
{
  "property": {
    "personCount": 3,
    "persons": [{"orignalPicture":"","fullPicture":"","targetPicture":"","box":{"x":100,"y":120,"width":80,"height":160}}]
  }
}
```

## 响应样例

```json
{"resCode":1,"resMsg":[]}
```

## 接收端建议

- 接收端应返回 HTTP `200` 和合法 JSON，例如 `{"resCode":1,"resMsg":[]}`。当前发送端不接受其他 2xx 状态码或空响应体。
- 以 `messageId` 做重推幂等处理；`recordId` 可在同一跟踪目标的多个事件间复用，不宜单独作为事件去重键。
- 将 base64 图片解码保存；视频和结构化文件字段可能是设备本地路径，不应直接当作可访问的 URL。视频可能仍在生成，需按部署提供的文件访问方式延迟读取和重试。
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

本文补齐了打包 HTML 的根字段及属性示例，并依据当前 `AlarmPushServiceImpl.cc` 修正传输细节。属性示例中的枚举值仅为示例；实际字段取决于算法输出。
