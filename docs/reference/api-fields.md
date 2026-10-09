---
title: 字段级 API 参考
description: 当前源码中可确认的通用字段、事件字段、系统集成参数和接口路由字段说明。
prev:
  text: 图片检测 API 接入指南
  link: /reference/image-detection-api
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
| `resMsg[].messageKey` | string | 稳定的本地化键；前端应优先用于翻译 |
| `resMsg[].details` | object | 实际值、限制值、所需资源和当前可用资源等机器可读上下文 |
| `resMsg[].retryable` | boolean | 外部条件变化后原操作是否值得重试 |
| `resMsg[].retryAfterSeconds` | number | 建议等待秒数；仅在适用时返回 |
| `resMsg[].recommendedAction` | string | 建议下一步，例如释放空间、改用分片或缩放图片 |
| `resultCode` | string | ChinaMobile 兼容响应码 |
| `resultMsg` | string | ChinaMobile 兼容响应文本 |
| `resData` | object | 业务响应数据 |

`messageKey`、`details`、`recommendedAction` 和 `retryAfterSeconds` 仅在适用时返回；`retryable` 为 `false` 时也可能省略。客户端必须把缺失字段按“未提供额外提示”处理，不能把它解释成新的失败。

## 分片上传字段

上传能力、分片和取消接口见 [API 概览：资源感知传输](api.md#资源感知传输)。`uploadTemp` 使用 `multipart/form-data`：

| 字段 | 类型 | 首片 | 后续分片 | 说明 |
| --- | --- | --- | --- | --- |
| `file` | blob | 必填 | 必填 | 当前分片；文件名必须保持一致 |
| `purpose` | string | 必填 | 必填 | `model-component`、`model-archive`、`video`、`face-import`、`audio`、`algorithm`、`upgrade` 或 `image` |
| `chunkIndex` | decimal string | `0` | 必填 | 从 0 开始的分片序号 |
| `totalChunks` | decimal string | 必填 | 必填 | 完整文件的分片数 |
| `totalSize` | decimal string | 必填 | 必填 | 完整文件字节数 |
| `chunkSize` | decimal string | 必填 | 必填 | 当前分片字节数，必须与 multipart 文件实际大小一致 |
| `clientRequestId` | string | 建议必填 | 保持不变 | 当前用户范围内稳定的恢复标识，最长 128 字符 |
| `uploadId` | string | 不填 | 必填 | 服务端签发的不透明会话标识 |
| `sha256` | string | 可选 | 保持不变 | 完整文件的 64 位小写或大写十六进制 SHA-256 |

`contentLength`、`fileName` 和 `filePath` 由服务端 multipart 解析器根据当前请求生成，客户端提交同名字段不会成为可信来源。

`uploadTemp` 的 `resData`：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `uploadId` | string | 后续分片和业务消费必须使用的服务端会话标识 |
| `nextChunkIndex` | decimal string | 服务端下一块所需序号；可能因幂等重放或重启恢复而跳过已确认分片 |
| `complete` | boolean | 完整文件是否已接收并校验 |
| `filePath` | string | R1 兼容的 `upload://` 不透明别名；不是服务器文件路径，新客户端不要使用 |

完成上传后，业务接口只引用会话标识：

| 业务接口 | 字段 |
| --- | --- |
| `/gtw/cwai/Camera/AddVideo` | `uploadId` |
| `/gtw/cwai/aihost/PTaskDetectPic` | 主图 `uploadId`；双图场景还需 `referenceImage.uploadId`。每一侧与该侧的 `imageBase64`/`imageUrl` 互斥 |
| `/gtw/cwai/Library/ModifyFacePicLib` | `pictureUploadIds[]` |
| `/gtw/cwai/BodyLibrary/DetectPerson` | `uploadId` |
| `/gtw/cwai/ThingsLibrary/AddLibThings` | `thingsList[].pictureUploadId` |

模型组件、模型归档、算法包、升级包、音频和人脸导入包也使用各自 DTO 中的 `uploadId` 字段。旧版 Base64 和兼容字段仍可读取，但大文件和高清图片客户端应使用分片会话，不能依赖服务器路径。

## 图片分析字段

来源：`MsgPTaskCreateRecv`、`MsgPTaskDetectPicRecv`、`MsgPTaskDetectPicSend` 和 `MsgMatchInfo`。完整时序、示例和错误处理见[图片检测 API 接入指南](image-detection-api.md)。

`POST /gtw/cwai/aihost/PTaskDetectPic` 请求：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `algorithmCode`、`taskId` | string | 与创建任务时一致的场景和任务 ID；建议显式指定 `taskId` |
| `uploadId` / `imageBase64` / `imageUrl` | string | 主图输入，三选一 |
| `referenceImage` | object | 双图场景的图片 B，同样三选一；单图场景不接受第二张图 |
| `taskConfig.params` | object[] | 本次请求覆盖，元素为 `{key, value}`；值按字符串传递，不保存为场景默认值 |
| `resultMode` | string | 默认 `business`；`debug` 附加诊断；`legacy` 仅用于单图旧协议兼容 |
| `needRetImg` | boolean | 是否生成结果图片 |
| `requestId` | string | 调用方关联本次分析的标识 |

图片没有区域；不接受非空 `taskConfig.areas` 或 `shieldedAreas`。任务创建也可以携带 `taskConfig`。底库绑定使用 `param.faceSet` / `param.workClothesSet`，双图阈值使用 `pair.threshold`，详见 API 指南的参数说明。

`resData` 关键结果字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schemaVersion` | number | 业务/调试响应为 `2` |
| `status` | string | 执行状态；执行完成不代表所有业务规则命中 |
| `outputs[]` | object[] | `nodeId`、`name`、`decision`、`targetIds`、`matchedCount`，未知结论还带 `reason` |
| `targetList[]` | object[] | 主图目标、像素坐标框、推理信息与规则结果；业务目标以输出节点的 `targetIds` 为准 |
| `targetList[].matchInfo` | object | `matched`、`matchDegree`、`setPicCount`、样本/分组标识及可选姓名、底库图片；见[底库详情](image-detection-api.md#底库命中详情与业务结论) |
| `comparison` | object | 双图成功结果：`nodeId`、`featureType`、`decision`、`score`，以及配置时的 `threshold` |
| `referenceTargetList[]` | object[] | 图片 B 的目标；坐标基于图片 B 原图 |
| `fullPicture`、`referencePicture` | string | A、B 的结果图片；关闭图片返回时为空 |
| `nodes[]` | object[] | 节点诊断；双图的 `imageSide` 标记 `A`、`B` 或 `both` |
| `errorNodeId`、`errorSide` | string | 执行失败时按适用情况提供；同时检查顶层 `resCode` 和 `resMsg[]` |

`outputs[].reason=no_comparable_samples` 表示没有有效底库比对证据，`insufficient_evidence` 表示其他判断依据不足。双图只输出分数时 `comparison.decision=score_only`；合法的 `score=0` 是成功结果，不能与错误或缺失分数混淆。

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
| `orignalPicture` | string | 原始图片；HTTP webhook 中为 Base64，内部消息中为 URL。字段名沿用当前实现 |
| `fullPicture` | string | 全景图；HTTP webhook 中为 Base64，内部消息中为 URL |
| `detectedPicture` | string | 检测目标图；HTTP webhook 中为 Base64，内部消息中为 URL |
| `video` | string | 告警视频；独立运行模式的 HTTP webhook 中为设备本地绝对路径，浏览器/查询接口中为 Web URL。文件可能在事件推送后数秒内才完成 |
| `videostructured` | string | 视频结构化文件路径或 URL，可为空 |
| `overviewFile` | string | 结构化概览文件路径或 URL，可为空 |
| `recordId` | string | 告警记录 ID |
| `files` | string[] | 关联文件列表（DTO 中定义；当前出站 `to_json` 不输出此字段，仅入站反序列化时读取） |
| `isRetryMessage` | boolean | 是否为重试消息 |
| `targets` | object[] | 触发事件的检测目标；包含 `label`、`confidence`、可选 `trackId` 和像素坐标 `box` |
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
