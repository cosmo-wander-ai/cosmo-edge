---
title: 字段级 API 参考
description: 当前源码中可确认的通用字段、事件字段、系统集成参数和接口路由字段说明。
prev:
  text: API 概览
  link: /reference/api
next:
  text: MQTT 接入参考
  link: /reference/mqtt
---

# 字段级 API 参考

本文从当前 DTO 和路由实现中提炼字段级说明，重点覆盖公开集成最容易用到的通用响应、事件查询、事件记录、HTTP 推送参数和 MQTT 参数。完整 OpenAPI schema 后续可以基于这些 DTO 自动生成。

## 通用响应

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `resCode` | number | CWAI 响应码，`1` 成功，`0` 失败 |
| `resMsg` | object[] | 错误或提示信息列表 |
| `resMsg[].msgCode` | string | 消息码 |
| `resMsg[].msgText` | string | 消息文本 |
| `resultCode` | string | ChinaMobile 兼容响应码 |
| `resultMsg` | string | ChinaMobile 兼容响应文本 |
| `resData` | object | 业务响应数据 |

## 分页和时间范围

事件查询等接口复用分页和时间字段：

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `pageNum` | number | `1` | 页码 |
| `pageSize` | number | `10` | 每页数量 |
| `timeBegin` | number | `0` | 开始时间，毫秒时间戳 |
| `timeEnd` | number | `0` | 结束时间，毫秒时间戳 |

## 事件查询条件

来源：`MsgConditionEvent`。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `algorithmCodes` | string[] | 算法编码列表 |
| `categorys` | string[] | 事件类别列表，字段名沿用当前实现 |
| `videoChannelName` | string | 通道名称 |
| `personName` | string | 人员名称 |
| `personCode` | string | 人员编号 |
| `matchLibName` | string | 匹配底库名称 |
| `propColor` | string | 目标颜色，常用于车身颜色 |
| `propRelatedColor` | string | 关联目标颜色，常用于车牌颜色 |
| `propType` | string | 目标类型，常用于车辆类型 |
| `propDirection` | string | 目标方向，常用于车辆方向 |
| `reportStatus` | number | 上报状态，默认 `-1` |

## 事件记录

来源：`MsgEventUnit`。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | 事件记录 ID |
| `videoChannelId` | string | 视频通道 ID |
| `channelCode` | string | 通道编码 |
| `channelName` | string | 通道名称 |
| `timestamp` | number | 事件时间，毫秒时间戳 |
| `category` | string | 事件类别 |
| `algorithmCode` | string | 算法编码 |
| `algorithmName` | string | 算法名称 |
| `areaId` | string | 区域 ID |
| `areaName` | string | 区域名称 |
| `fullPicture` | string | 全景图 URL |
| `detectedPicture` | string | 检测目标图 URL |
| `video` | string | 告警视频 URL |
| `videostructured` | string | 结构化视频文件 URL |
| `reportStatus` | number | 上报状态 |
| `property` | string | 属性 JSON 字符串，按算法类型变化 |

## 事件上报负载

HTTP webhook 和部分内部事件消息使用 `CMsgOnEventsReq` 语义：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `messageId` | string | 消息 ID |
| `devId` | string | 设备 ID |
| `taskId` | string | 任务 ID |
| `videoChannelId` | string | 通道 ID |
| `channelName` | string | 通道名称 |
| `timestamp` | string | UTC 毫秒时间戳字符串 |
| `itimestamp` | number | UTC 毫秒时间戳（DTO 中定义；当前出站 `to_json` 不输出此字段，仅入站反序列化时读取） |
| `algorithmId` | string | 算法 ID |
| `algorithmCode` | string | 算法编码 |
| `algorithmName` | string | 算法名称 |
| `areaId` | string | 区域 ID |
| `areaName` | string | 区域名称 |
| `orignalPicture` | string | 原始图片 URL，字段名沿用当前实现 |
| `fullPicture` | string | 全景图 URL |
| `detectedPicture` | string | 检测目标图 URL |
| `video` | string | 告警视频 URL |
| `videostructured` | string | 视频结构化文件 URL |
| `overviewFile` | string | 结构化概览文件 URL |
| `recordId` | string | 告警记录 ID |
| `files` | string[] | 关联文件列表（DTO 中定义；当前出站 `to_json` 不输出此字段，仅入站反序列化时读取） |
| `isRetryMessage` | boolean | 是否为重试消息 |
| `property` | object | 属性对象，按算法类型变化 |
| `category` | string | 事件类别 |

## 属性字段类型

事件属性通过 `OnEventsPropertyType` 区分（枚举见 `src/util/MsgBaseTypes.h`，出站序列化见 `src/util/dto/ClientMsgEvent.cc`）。每种类型输出对应的 JSON 键：

| 类型 (`OnEventsPropertyType`) | 输出键 | 主要字段 |
| --- | --- | --- |
| `face` | `face` | `quality`、`age`、`gender`、`wearMask`、`wearGlasses`、`featureUrl`、`image` |
| `body` (Body / BodyFeature) | `body` | `topLength`、`topColor`、`bottomLength`、`bottomColor`、`featureUrl`、`image` |
| `vehicle` | `vehicle` | `plateColor`、`vehicleColor`、`vehicleClass`、`orientation`、`plate`、`plateSrc`、`attrs` |
| `behavior` | `behavior` | `count`、`duration`、`targetId` |
| `machineMaterial` | `machineMaterial` | `matchId`、`matchDegree`、`groupId`、`groupName`、`baseImageUrl`、`runningStatus` |
| `people` | `people` | `enterNumber`、`leaveNumber`、`enterOrgNum`、`leaveOrgNum`、`time` |
| `car` | `car` | `enterNumber`、`leaveNumber`、`enterOrgNum`、`leaveOrgNum`、`time` |
| `workClothesRecognition` | `workClothesRecognition` | `matchId`、`matchDegree`、`groupId`、`groupName`、`baseImageUrl` |
| `personCount` (PersonCount) | `personCount` + `persons` | 区域人数统计；同时输出 `persons` 人员列表（字段见下） |
| `countNumber` (CountNumber) | `countNumber` | 计数类事件 |

以下为**附加子对象**（不是独立的 `OnEventsPropertyType` 枚举值，而是随主类型一起输出）：

| 子对象 | 出现条件 | 主要字段 |
| --- | --- | --- |
| `recognition` | `face` 类型同时输出 | `matchDegree`、`matchLibName`、`matchId`、`LibImage`、`matchName`、`personCode`、`personId` |
| `persons` | `personCount` 类型同时输出 | `orignalPicture`、`fullPicture`、`targetPicture`、`box` |
| `target` | 任意类型，当 `bHaveTarget` 为真时附加 | `inAreaTime`、`inAreaFullImageUrl`、`outAreaTime`、`outAreaFullImageUrl` |

## HTTP 推送参数

路由：

```text
/gtw/cwai/System/QueryHttpInterfaceParam
/gtw/cwai/System/SetHttpInterfaceParam
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `switch` | boolean | 是否启用 HTTP 推送；**设置接口只识别此字段** |
| `enable` | boolean | **仅查询响应输出**（与 `switch` 同值）；设置接口不读取此字段 |
| `url` | string | 接收事件的 HTTP URL |

## MQTT 参数

路由：

```text
/gtw/cwai/System/QueryMqttAdapterParam
/gtw/cwai/System/SetMqttAdapterParam
```

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `switch` | boolean | `true` | 是否启用 MQTT；**设置接口只识别此字段** |
| `enable` | boolean | `true` | **仅查询响应输出**（与 `switch` 同值）；设置接口不读取此字段 |
| `url` | string | 空 | MQTT Broker 地址 |
| `port` | number | `1883` | MQTT Broker 端口 |
| `status` | boolean | `true` | 当前 MQTT 注册/连接状态，查询结果字段 |
| `authMode` | number | `0` | `0` 使用内置 IoT 认证，非 `0` 使用普通用户名密码 |
| `clientId` | string | 空 | 普通认证模式下的 client id |
| `userName` | string | 空 | 普通认证模式下的用户名 |
| `passwd` | string | 空 | 普通认证模式下的密码 |

## IoT 网络模式参数

路由：

```text
/gtw/cwai/System/QueryIotNetworkParam
/gtw/cwai/System/ModifyIotNetworkParam
```

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `mqttIp` | string | 空 | IoT 网络模式下 MQTT 地址 |
| `mqttPort` | number | `1883` | IoT 网络模式下 MQTT 端口 |
| `httpUrl` | string | 空 | IoT 网络模式下 HTTP 地址 |
| `status` | boolean | `true` | 当前 MQTT 是否启用，查询结果字段 |
