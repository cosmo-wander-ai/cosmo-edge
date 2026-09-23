---
title: HTTP Webhook Reference
description: Current HTTP event push parameters, event payload fields, property objects, and receiver recommendations.
prev:
  text: MQTT Reference
  link: /en/reference/mqtt
next:
  text: Models and Resources
  link: /en/reference/models
---

# HTTP Webhook Reference

CosmoEdge supports pushing alarm/event data to an external platform over HTTP. The push configuration is currently maintained through system interfaces, and the event payload fields come from the current event DTO and the packaged HTML interface documentation.

## Configuration API

Query:

```text
/gtw/cwai/System/QueryHttpInterfaceParam
```

Set:

```text
/gtw/cwai/System/SetHttpInterfaceParam
```

Set request example:

```json
{
  "switch": true,
  "url": "http://example.com/cosmo/events"
}
```

Query response example:

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

Fields:

| Field | Type | Description |
| --- | --- | --- |
| `switch` | boolean | Whether HTTP push is enabled. |
| `enable` | boolean | Whether HTTP push is enabled; compatibility field in the query response. |
| `url` | string | Receiver URL. |

## Basic Information

| Field | Value |
| --- | --- |
| Request URL | URL configured by the user on the platform page |
| Request Method | POST |
| Content-Type | application/json |
| Success criteria | HTTP `200` with a valid JSON response. |

The sender requires HTTP `200` and a JSON body that can be decoded as the response DTO; an empty body is not sufficient. The current sender does not additionally check the business `resCode`, but receivers should return `{"resCode":1,"resMsg":[]}` on success.

Only `orignalPicture`, `fullPicture`, `detectedPicture`, `property.face.image`, and `property.recognition.LibImage` are explicitly converted from local files to base64 by `AlarmPushServiceImpl::OnEvents()`. Missing or unreadable images can become empty strings. Do not assume all nested image fields are base64: `property.body.image`, `persons` image fields, and other algorithm properties are passed through as supplied. Video and structured-file paths are not converted into public URLs.

## Event Payload

A typical event payload:

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

For field details, see [API Fields](api-fields.md#event-report-payload).

> Note: The DTO also defines the `itimestamp` and `files` fields, but the current outbound `to_json` serialization does not emit them, so they are intentionally omitted from the payload above.

## Root Fields

| Field | Type | Description |
| --- | --- | --- |
| `messageId` | string | Unique event ID for tracing and retry deduplication. |
| `devId` | string | Device ID. |
| `taskId` | string | Task ID. |
| `videoChannelId` | string | Channel ID. |
| `channelName` | string | Channel name. |
| `timestamp` | string | 13-digit millisecond timestamp. |
| `algorithmId` | string | Algorithm ID. |
| `algorithmCode` | string | Algorithm code. In the current implementation, this is usually the same as `algorithmId`. |
| `algorithmName` | string | Algorithm name. |
| `areaId` | string | Region ID where the event occurred. |
| `areaName` | string | Region name where the event occurred. |
| `orignalPicture` | string | Original full-scene snapshot. The field name is historically spelled as `orignalPicture` for backward compatibility. |
| `fullPicture` | string | Full-scene snapshot with detection bounding boxes overlaid. |
| `detectedPicture` | string | Cropped detection target image; may be empty. |
| `video` | string | Alarm video file path; may be empty. |
| `videostructured` | string | File path for structured data (targets and regions) corresponding to the video; may be empty. |
| `overviewFile` | string | Event process overview file path; may be empty. |
| `recordId` | string | Target or alarm record ID. When tracking is enabled, this usually remains the same for the same target. |
| `isRetryMessage` | boolean | Whether this is an offline retry message. |
| `category` | string | Algorithm category. |
| `property` | object | Algorithm extended attributes. Only present when the corresponding algorithm has extended attributes. |

## Detection Targets

`targets` lists the detection targets that triggered the event:

| Field | Type | Description |
| --- | --- | --- |
| `label` | string | Model detection label, such as `category0`, `person`, or `Pedestrian` |
| `confidence` | number | Detection confidence |
| `trackId` | string | Target tracking ID; may be omitted when tracking is disabled |
| `box` | object | Pixel-coordinate bounding box with `x`, `y`, `width`, and `height` |

Aggregate events without individual targets may omit `targets`. Initial HTTP delivery and offline retries use the same structure.

## Property Object

`property` changes with the algorithm type. The property categories that can currently be confirmed include:

| Category | Description |
| --- | --- |
| `face` | Face quality, age, gender, mask, glasses, feature files, and face image. |
| `body` | Body attributes, body features, and body image. |
| `vehicle` | License plate, vehicle color, vehicle type, direction, and vehicle attributes. |
| `behavior` | Behavior count, duration, and target ID. |
| `machineMaterial` | Material/equipment status match result. |
| `people` | People flow statistics. |
| `car` | Vehicle flow statistics. |
| `workClothesRecognition` | Work-clothes recognition match result. |
| `personCount` | Person count. |
| `countNumber` | Count number. |

Additional sub-objects may appear alongside the main categories:

| Sub-object | Description |
| --- | --- |
| `recognition` | Face-library match result; accompanies `face`. |
| `persons` | Person target list; accompanies `personCount`. |
| `target` | Target enter/exit area time and image; optional across types. |

## property Output Rules

`property` outputs the corresponding sub-object based on algorithm type. Not all sub-objects appear simultaneously.

### Face Detection and Recognition

```json
{
  "property": {
    "face": {"quality":92.5,"age":25,"gender":0,"wearMask":0,"wearGlasses":0,"featureUrl":"","image":"/9j/4AAQ..."},
    "recognition": {"matchDegree":95.5,"matchLibName":"人脸库","matchId":"001","LibImage":"/9j/4AAQ...","matchName":"张三","personCode":"10002","personId":"10002"}
  }
}
```

### Body Attributes

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

### Vehicle Attributes

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

### Behavior Attributes

```json
{"property":{"behavior":{"targetId":"qsddf123","duration":300,"count":10}}}
```

### Machine/Material and Work Uniform Matching

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

### People Count, Vehicle Flow, Single-Frame Count, and Crowd Gathering

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

## Response Sample

```json
{"resCode":1,"resMsg":[]}
```

## Receiver Recommendations

- The receiver should return HTTP `200` with a valid JSON response, such as `{"resCode":1,"resMsg":[]}`. Other 2xx statuses and empty response bodies are not accepted by the current sender.
- Use `messageId` for retry deduplication. A `recordId` may be shared by multiple events for the same tracked target and should not be the sole event deduplication key.
- Decode and save base64 images. Video and structured-file fields may be device-local paths, not accessible URLs. Use the file-access mechanism provided by your deployment, allowing for video generation delays and retries.
- Do not assume all fields in `property` are present; different algorithms only populate the relevant fields.
- The current implementation retains legacy field names such as `orignalPicture`; receivers must be tolerant of them.

## Packaged HTML Reference

The repository currently retains:

```text
data/Interface/ai-box-interface_v1.0.html
```

After installation, the entry point is:

```text
web/staticfile/httpInterface.html
```

This page includes the packaged HTML root fields and property examples, with transport details checked against `AlarmPushServiceImpl.cc`. Enum values are illustrative; actual fields depend on algorithm output.
