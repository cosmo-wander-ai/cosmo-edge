---
title: API Fields
description: Field-level references for common responses, events, system integration parameters, and route fields verified from the current source.
prev:
  text: API Overview
  link: /en/reference/api
next:
  text: MQTT Reference
  link: /en/reference/mqtt
---

# API Fields

This page distills field-level details from the current DTO and route implementations, focusing on the common responses, event queries, event records, HTTP push parameters, and MQTT parameters that are most commonly used by public integrations. A complete OpenAPI schema can be generated from these DTOs later.

## Common Response

| Field | Type | Description |
| --- | --- | --- |
| `resCode` | number | CWAI response code; `1` = success, `0` = failure |
| `resMsg` | object[] | Error or info message list |
| `resMsg[].msgCode` | string | Message code |
| `resMsg[].msgText` | string | Message text |
| `resultCode` | string | ChinaMobile-compatible response code |
| `resultMsg` | string | ChinaMobile-compatible response text |
| `resData` | object | Business response data |

## Pagination and Time Range

Event queries and similar interfaces reuse the pagination and time fields:

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `pageNum` | number | `1` | Page number |
| `pageSize` | number | `10` | Page size |
| `timeBegin` | number | `0` | Start time, millisecond timestamp |
| `timeEnd` | number | `0` | End time, millisecond timestamp |

## Event Query Conditions

Source: `MsgConditionEvent`.

| Field | Type | Description |
| --- | --- | --- |
| `algorithmCodes` | string[] | Algorithm code list |
| `categorys` | string[] | Event category list (field name retained from the current implementation) |
| `videoChannelName` | string | Channel name |
| `personName` | string | Person name |
| `personCode` | string | Person code |
| `matchLibName` | string | Matched gallery name |
| `propColor` | string | Target color, often vehicle color |
| `propRelatedColor` | string | Related target color, often plate color |
| `propType` | string | Target type, often vehicle type |
| `propDirection` | string | Target direction, often vehicle direction |
| `reportStatus` | number | Report status, default `-1` |

## Event Record

Source: `MsgEventUnit`.

| Field | Type | Description |
| --- | --- | --- |
| `id` | string | Event record ID |
| `videoChannelId` | string | Video channel ID |
| `channelCode` | string | Channel code |
| `channelName` | string | Channel name |
| `timestamp` | number | Event time, millisecond timestamp |
| `category` | string | Event category |
| `algorithmCode` | string | Algorithm code |
| `algorithmName` | string | Algorithm name |
| `areaId` | string | Area ID |
| `areaName` | string | Area name |
| `fullPicture` | string | Full-frame image URL |
| `detectedPicture` | string | Target detection image URL |
| `video` | string | Alarm video URL |
| `videostructured` | string | Structured video file URL |
| `reportStatus` | number | Report status |
| `property` | string | Attribute JSON string; varies by algorithm type |

## Event Report Payload

HTTP webhook and some internal event messages use `CMsgOnEventsReq` semantics:

| Field | Type | Description |
| --- | --- | --- |
| `messageId` | string | Message ID |
| `devId` | string | Device ID |
| `taskId` | string | Task ID |
| `videoChannelId` | string | Channel ID |
| `channelName` | string | Channel name |
| `timestamp` | string | UTC millisecond timestamp string |
| `itimestamp` | number | UTC millisecond timestamp (defined in the DTO; the current outbound `to_json` does not output this field, only inbound deserialization reads it) |
| `algorithmId` | string | Algorithm ID |
| `algorithmCode` | string | Algorithm code |
| `algorithmName` | string | Algorithm name |
| `areaId` | string | Area ID |
| `areaName` | string | Area name |
| `orignalPicture` | string | Original image URL (field name retained from the current implementation) |
| `fullPicture` | string | Full-frame image URL |
| `detectedPicture` | string | Target detection image URL |
| `video` | string | Alarm video URL |
| `videostructured` | string | Structured video file URL |
| `overviewFile` | string | Structured overview file URL |
| `recordId` | string | Alarm record ID |
| `files` | string[] | Related file list (defined in the DTO; the current outbound `to_json` does not output this field, only inbound deserialization reads it) |
| `isRetryMessage` | boolean | Whether this is a retry message |
| `property` | object | Attribute object; varies by algorithm type |
| `category` | string | Event category |

## Property Field Types

Event properties are differentiated via `OnEventsPropertyType` (see the enum in `src/util/MsgBaseTypes.h`, and the outbound serialization in `src/util/dto/ClientMsgEvent.cc`). Each type outputs its corresponding JSON key:

| Type (`OnEventsPropertyType`) | Output Key | Main Fields |
| --- | --- | --- |
| `face` | `face` | `quality`, `age`, `gender`, `wearMask`, `wearGlasses`, `featureUrl`, `image` |
| `body` (Body / BodyFeature) | `body` | `topLength`, `topColor`, `bottomLength`, `bottomColor`, `featureUrl`, `image` |
| `vehicle` | `vehicle` | `plateColor`, `vehicleColor`, `vehicleClass`, `orientation`, `plate`, `plateSrc`, `attrs` |
| `behavior` | `behavior` | `count`, `duration`, `targetId` |
| `machineMaterial` | `machineMaterial` | `matchId`, `matchDegree`, `groupId`, `groupName`, `baseImageUrl`, `runningStatus` |
| `people` | `people` | `enterNumber`, `leaveNumber`, `enterOrgNum`, `leaveOrgNum`, `time` |
| `car` | `car` | `enterNumber`, `leaveNumber`, `enterOrgNum`, `leaveOrgNum`, `time` |
| `workClothesRecognition` | `workClothesRecognition` | `matchId`, `matchDegree`, `groupId`, `groupName`, `baseImageUrl` |
| `personCount` (PersonCount) | `personCount` + `persons` | Area person-count statistics; also outputs the `persons` list (fields below) |
| `countNumber` (CountNumber) | `countNumber` | Counting-type events |

The following are **additional sub-objects** (not independent `OnEventsPropertyType` enum values, but emitted alongside the main type):

| Sub-object | Emitted When | Main Fields |
| --- | --- | --- |
| `recognition` | Emitted alongside the `face` type | `matchDegree`, `matchLibName`, `matchId`, `LibImage`, `matchName`, `personCode`, `personId` |
| `persons` | Emitted alongside the `personCount` type | `orignalPicture`, `fullPicture`, `targetPicture`, `box` |
| `target` | Any type, appended when `bHaveTarget` is true | `inAreaTime`, `inAreaFullImageUrl`, `outAreaTime`, `outAreaFullImageUrl` |

## HTTP Push Parameters

Routes:

```text
/gtw/cwai/System/QueryHttpInterfaceParam
/gtw/cwai/System/SetHttpInterfaceParam
```

| Field | Type | Description |
| --- | --- | --- |
| `switch` | boolean | Whether HTTP push is enabled; **the set interface only recognizes this field** |
| `enable` | boolean | **Only output by the query response** (same value as `switch`); the set interface does not read this field |
| `url` | string | HTTP URL that receives events |

## MQTT Parameters

Routes:

```text
/gtw/cwai/System/QueryMqttAdapterParam
/gtw/cwai/System/SetMqttAdapterParam
```

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `switch` | boolean | `true` | Whether MQTT is enabled; **the set interface only recognizes this field** |
| `enable` | boolean | `true` | **Only output by the query response** (same value as `switch`); the set interface does not read this field |
| `url` | string | empty | MQTT broker address |
| `port` | number | `1883` | MQTT broker port |
| `status` | boolean | `true` | Current MQTT registration/connection status; a query-result field |
| `authMode` | number | `0` | `0` uses built-in IoT auth; non-`0` uses a normal username/password |
| `clientId` | string | empty | Client ID under normal auth mode |
| `userName` | string | empty | Username under normal auth mode |
| `passwd` | string | empty | Password under normal auth mode |

## IoT Network Mode Parameters

Routes:

```text
/gtw/cwai/System/QueryIotNetworkParam
/gtw/cwai/System/ModifyIotNetworkParam
```

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `mqttIp` | string | empty | MQTT address under IoT network mode |
| `mqttPort` | number | `1883` | MQTT port under IoT network mode |
| `httpUrl` | string | empty | HTTP address under IoT network mode |
| `status` | boolean | `true` | Whether MQTT is currently enabled; a query-result field |
