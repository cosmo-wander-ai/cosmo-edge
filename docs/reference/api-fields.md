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
| `itimestamp` | number | UTC 毫秒时间戳 |
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
| `files` | string[] | 关联文件列表 |
| `isRetryMessage` | boolean | 是否为重试消息 |
| `property` | object | 属性对象，按算法类型变化 |
| `category` | string | 事件类别 |

## 属性字段类型

事件属性通过 `OnEventsPropertyType` 区分：

| 类型 | 说明 | 主要字段 |
| --- | --- | --- |
| `face` | 人脸检测 | `quality`、`age`、`gender`、`wearMask`、`wearGlasses`、`featureUrl`、`image` |
| `recognition` | 人脸识别 | `matchDegree`、`matchLibName`、`matchId`、`LibImage`、`matchName`、`personCode`、`personId` |
| `body` | 人体属性或特征 | `topLength`、`topColor`、`bottomLength`、`bottomColor`、`featureUrl`、`image` |
| `vehicle` | 车辆属性 | `plateColor`、`vehicleColor`、`vehicleClass`、`orientation`、`plate`、`plateSrc`、`attrs` |
| `behavior` | 行为类事件 | `count`、`duration`、`targetId` |
| `machineMaterial` | 物料或设备状态 | `matchId`、`matchDegree`、`groupId`、`groupName`、`baseImageUrl`、`runningStatus` |
| `people` | 人流统计 | `enterNumber`、`leaveNumber`、`enterOrgNum`、`leaveOrgNum`、`time` |
| `car` | 车流统计 | `enterNumber`、`leaveNumber`、`enterOrgNum`、`leaveOrgNum`、`time` |
| `workClothesRecognition` | 工服识别 | `matchId`、`matchDegree`、`groupId`、`groupName`、`baseImageUrl` |
| `persons` | 人员列表 | `orignalPicture`、`fullPicture`、`targetPicture`、`box` |
| `target` | 目标进出区域 | `inAreaTime`、`inAreaFullImageUrl`、`outAreaTime`、`outAreaFullImageUrl` |

## HTTP 推送参数

路由：

```text
/gtw/cwai/System/QueryHttpInterfaceParam
/gtw/cwai/System/SetHttpInterfaceParam
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `switch` | boolean | 是否启用 HTTP 推送，设置接口使用该字段 |
| `enable` | boolean | 是否启用 HTTP 推送，查询结果会同时输出 `enable` 和 `switch` |
| `url` | string | 接收事件的 HTTP URL |

## MQTT 参数

路由：

```text
/gtw/cwai/System/QueryMqttAdapterParam
/gtw/cwai/System/SetMqttAdapterParam
```

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `switch` | boolean | `true` | 是否启用 MQTT，设置接口使用该字段 |
| `enable` | boolean | `true` | 是否启用 MQTT，查询结果会同时输出 `enable` 和 `switch` |
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
