---
title: API Fields
description: Common response, pagination, event, webhook, MQTT, and network field references.
prev:
  text: API Overview
  link: /en/reference/api
next:
  text: MQTT Reference
  link: /en/reference/mqtt
---

# API Fields

This page collects field-level conventions that are repeatedly used by HTTP, MQTT, and webhook integrations. Field names intentionally follow the current implementation, including legacy spellings (e.g., `categorys`, `orignalPicture`).

## Common Response

Most management responses inherit from `MsgSendHead`:

| Field | Type | Notes |
| --- | --- | --- |
| `resCode` | number | CWAI response code: `1` = success, `0` = failure. |
| `resMsg` | object[] | Error or info message list. |
| `resMsg[].msgCode` | string | Message code. |
| `resMsg[].msgText` | string | Message text. |
| `resultCode` | string | ChinaMobile-compatible response code. |
| `resultMsg` | string | ChinaMobile-compatible response text. |
| `resData` | object | Business response payload. |

## Pagination and Time Range

Event queries and similar endpoints use:

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `pageNum` | number | `1` | Page number. |
| `pageSize` | number | `10` | Page size. |
| `timeBegin` | number | `0` | Start time, millisecond timestamp. |
| `timeEnd` | number | `0` | End time, millisecond timestamp. |

## Event Query Conditions

Source: `MsgConditionEvent`.

| Field | Type | Notes |
| --- | --- | --- |
| `algorithmCodes` | string[] | Algorithm code list. |
| `categorys` | string[] | Event category list (legacy spelling retained). |
| `videoChannelName` | string | Channel name. |
| `personName` | string | Person name. |
| `personCode` | string | Person code. |
| `matchLibName` | string | Matched gallery name. |
| `propColor` | string | Target color (often vehicle color). |
| `propRelatedColor` | string | Related target color (often plate color). |
| `propType` | string | Target type (often vehicle type). |
| `propDirection` | string | Target direction (often vehicle direction). |
| `reportStatus` | number | Report status, default `-1`. |

## Event Record

Source: `MsgEventUnit`.

| Field | Type | Notes |
| --- | --- | --- |
| `id` | string | Event record ID. |
| `videoChannelId` | string | Video channel ID. |
| `channelCode` | string | Channel code. |
| `channelName` | string | Channel name. |
| `timestamp` | number | Event time, millisecond timestamp. |
| `category` | string | Event category. |
| `algorithmCode` | string | Algorithm code. |
| `algorithmName` | string | Algorithm name. |
| `areaId` | string | Area / region ID. |
| `areaName` | string | Area / region name. |
| `fullPicture` | string | Full-frame image URL. |
| `detectedPicture` | string | Target detection image URL. |
| `video` | string | Alarm video URL. |
| `videostructured` | string | Structured video file URL. |
| `reportStatus` | number | Report status. |
| `property` | string | Attribute JSON string; varies by algorithm type. |

## Event Report Payload (Webhook / Internal)

HTTP webhook and some internal event messages use `CMsgOnEventsReq` semantics:

| Field | Type | Notes |
| --- | --- | --- |
| `messageId` | string | Message ID. |
| `devId` | string | Device ID. |
| `taskId` | string | Task ID. |
| `videoChannelId` | string | Channel ID. |
| `channelName` | string | Channel name. |
| `timestamp` | string | UTC millisecond timestamp string. |
| `itimestamp` | number | UTC millisecond timestamp. |
| `algorithmId` | string | Algorithm ID. |
| `algorithmCode` | string | Algorithm code. |
| `algorithmName` | string | Algorithm name. |
| `areaId` | string | Area / region ID. |
| `areaName` | string | Area / region name. |
| `orignalPicture` | string | Original image URL (legacy spelling retained). |
| `fullPicture` | string | Full-frame image URL. |
| `detectedPicture` | string | Target detection image URL. |
| `video` | string | Alarm video URL. |
| `videostructured` | string | Structured video file URL. |
| `overviewFile` | string | Structured overview file URL. |
| `recordId` | string | Alarm record ID. |
| `files` | string[] | Related file list. |
| `isRetryMessage` | boolean | Whether this is a retry message. |
| `property` | object | Attribute object; varies by algorithm type. |
| `category` | string | Event category. |

## Property Field Types

Event properties are typed via `OnEventsPropertyType`:

| Type | Purpose | Key Fields |
| --- | --- | --- |
| `face` | Face detection | `quality`, `age`, `gender`, `wearMask`, `wearGlasses`, `featureUrl`, `image` |
| `recognition` | Face recognition | `matchDegree`, `matchLibName`, `matchId`, `LibImage`, `matchName`, `personCode`, `personId` |
| `body` | Body attributes / features | `topLength`, `topColor`, `bottomLength`, `bottomColor`, `featureUrl`, `image` |
| `vehicle` | Vehicle attributes | `plateColor`, `vehicleColor`, `vehicleClass`, `orientation`, `plate`, `plateSrc`, `attrs` |
| `behavior` | Behavior events | `count`, `duration`, `targetId` |
| `machineMaterial` | Material / device status | `matchId`, `matchDegree`, `groupId`, `groupName`, `baseImageUrl`, `runningStatus` |
| `people` | Pedestrian flow stats | `enterNumber`, `leaveNumber`, `enterOrgNum`, `leaveOrgNum`, `time` |
| `car` | Vehicle flow stats | `enterNumber`, `leaveNumber`, `enterOrgNum`, `leaveOrgNum`, `time` |
| `workClothesRecognition` | Work uniform recognition | `matchId`, `matchDegree`, `groupId`, `groupName`, `baseImageUrl` |
| `persons` | Person list | `orignalPicture`, `fullPicture`, `targetPicture`, `box` |
| `target` | Target enter/leave zone | `inAreaTime`, `inAreaFullImageUrl`, `outAreaTime`, `outAreaFullImageUrl` |

## HTTP Push Parameters

Routes:
```text
/gtw/cwai/System/QueryHttpInterfaceParam
/gtw/cwai/System/SetHttpInterfaceParam
```

| Field | Type | Notes |
| --- | --- | --- |
| `switch` | boolean | Whether HTTP push is enabled (write field). |
| `enable` | boolean | Whether HTTP push is enabled (read field; response includes both `enable` and `switch`). |
| `url` | string | Receiver HTTP URL. |

## MQTT Parameters

Routes:
```text
/gtw/cwai/System/QueryMqttAdapterParam
/gtw/cwai/System/SetMqttAdapterParam
```

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `switch` | boolean | `true` | Whether MQTT is enabled (write field). |
| `enable` | boolean | `true` | Whether MQTT is enabled (read field). |
| `url` | string | empty | MQTT broker address. |
| `port` | number | `1883` | MQTT broker port. |
| `status` | boolean | `true` | Current MQTT registration/connection status (read field). |
| `authMode` | number | `0` | `0` = built-in IoT auth; non-`0` = normal username/password. |
| `clientId` | string | empty | Client ID for normal auth mode. |
| `userName` | string | empty | Username for normal auth mode. |
| `passwd` | string | empty | Password for normal auth mode. |
