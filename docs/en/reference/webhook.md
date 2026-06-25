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

For field details, see [API Fields](api-fields.md#event-report-payload).

> Note: The DTO also defines the `itimestamp` and `files` fields, but the current outbound `to_json` serialization does not emit them, so they are intentionally omitted from the payload above.

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

## Receiver Recommendations

- The receiver should return `2xx` to indicate successful processing.
- Use `messageId` or `recordId` for idempotent processing.
- Perform lazy fetching with retry for image, video, and structured file URLs.
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

This HTML can be used as a historical interface reference, but open-source documentation should preferentially rely on the current DTO and Markdown references.
