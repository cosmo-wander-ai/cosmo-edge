---
title: MQTT Reference
description: Current MQTT topics, message envelope, registration, heartbeat, downstream requests, and response formats.
prev:
  text: API Fields
  link: /en/reference/api-fields
next:
  text: HTTP Webhook Reference
  link: /en/reference/webhook
---

# MQTT Reference

The current MQTT implementation is located at:

```text
src/network/mqtt
src/service/network/impl/MqttLifecycleServiceImpl.cc
```

The HTML reference packaged at runtime is located at:

```text
data/Interface/mqtt_v1.0.html
```

## Topics

| Direction | Topic | Description |
| --- | --- | --- |
| Device -> Platform | `/d2p/aibox` | Registration messages and normal responses. |
| Device -> Platform | `/d2p/aibox/heartbeat` | Heartbeat messages. |
| Platform -> Device | `/p2d/aibox/{deviceSn}` | Platform downstream business requests. |
| Platform -> Device | `/p2d/aibox/heartbeat/{deviceSn}` | Platform heartbeat-related downstream messages. |

After connecting, the device first subscribes to the two platform-to-device topics, then sends a registration message to `/d2p/aibox`. After successful registration, it sends a heartbeat to `/d2p/aibox/heartbeat` every 30 seconds.

## Connection Parameters

System configuration fields are described in [API Fields](api-fields.md#mqtt-parameters).

The current implementation supports two authentication modes:

| `authMode` | Behavior |
| --- | --- |
| `0` | Built-in IoT authentication. `clientId` uses the device SN, `username` is `aibox::{deviceSn}`, and `authKey` is `cwai`. |
| Non-`0` | Normal username/password authentication. Uses `clientId`, `userName`, and `passwd` from the configuration. |

`deviceType` in the connection parameters is fixed to `box`.

## Message Envelope

An MQTT envelope consists of `head` and `body`:

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

| Field | Type | Description |
| --- | --- | --- |
| `head.requestId` | string | Request ID. The response reuses this value. |
| `head.action` | string | Business interface path, for example `/gtw/cwai/System/QueryDeviceInfo`. |
| `head.deviceSn` | string | Device SN. The device rejects messages whose SN does not match. |
| `head.msgType` | string | `register`, `heartbeat`, `request`, or `response`. |
| `body` | string / object | Registration and heartbeat use an object. Business requests and responses use a JSON string; serialize the business JSON with `JSON.stringify` before placing it in this field. |

## Registration Message

The device sends to `/d2p/aibox`:

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

| body Field | Type | Required | Example | Description |
| --- | --- | --- | --- | --- |
| `devId` | string | Yes | `ai-123123123123` | Device ID, usually the same as the device SN. |
| `supplier` | string | No | `CWAI` | Supplier identifier. |
| `aiHostVersion` | string | No | `1.0.0` | AI core version number. |
| `engineType` | string | No | `CWNN` | Algorithm engine type. |
| `deviceModel` | string | No | `CWAI-AIBOX` | Device model. |
| `devType` | number | No | `2` | Device type; fill in according to the platform enumeration. |

### Platform to Device

topic: `/p2d/aibox/{Device SN}`

```json
{
  "head": {"requestId":"b5e5035f-9b1e-4578-b3ac-1bed11272c49","action":"","deviceSn":"ai-123123123123","msgType":"register"},
  "body": {"resCode":1,"resMsg":[],"resData":{}}
}
```

| body Field | Type | Required | Example | Description |
| --- | --- | --- | --- | --- |
| `resCode` | number | Yes | `1` | Registration result: 1 for success, 0 for failure. |
| `resMsg` | object[] | No | `[]` | Response message list. |
| `resData` | object | No | `{}` | Response data; usually an empty object for registration responses. |

## Heartbeat Message

The device sends to `/d2p/aibox/heartbeat`:

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

The heartbeat publish ACK timeout is 2000 ms. Repeated failures beyond a threshold trigger a reconnect.

| body Field | Type | Required | Example | Description |
| --- | --- | --- | --- | --- |
| `devId` | string | Yes | `ai-123123123123` | Device ID, usually the same as the device SN. |
| `hostStatus` | number | No | `0` | Device or AI core running status, reported per device enumeration. |
| `customScore` | number | No | `70` | Device custom health score. |
| `cpuUsage` | number | No | `0.8105` | CPU usage. |
| `memTotal` | number | No | `1024` | Total memory. |
| `memAvailable` | number | No | `1024` | Available memory. |
| `gpuUsage` | number | No | `0.8105` | GPU usage. |
| `gpuMemTotal` | number | No | `1024` | Total GPU memory. |
| `gpuMemAvailable` | number | No | `501` | Available GPU memory. |
| `diskTotal` | number | No | `10240` | Total disk space. |
| `diskAvailable` | number | No | `1024` | Available disk space. |

### Platform to Device

topic: `/p2d/aibox/heartbeat/{Device SN}`

```json
{
  "head": {"requestId":"84d92164-3f32-4c91-8c03-69b41f7b21db","action":"","deviceSn":"ai-123123123123","msgType":"heartbeat"},
  "body": {"resCode":1}
}
```

| body Field | Type | Required | Example | Description |
| --- | --- | --- | --- | --- |
| `resCode` | number | Yes | `1` | Heartbeat acknowledgment result: 1 for success, 0 for failure. |

## Platform Downstream Requests

The platform sends to `/p2d/aibox/{deviceSn}`:

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

After receiving it, the device:

1. Validates that `deviceSn` equals the local SN.
2. Validates that `msgType` is `request`.
3. Uses `head.action` as the API route.
4. Dispatches the `body` string as business JSON to the `ApiRouter`.
5. Returns the response via `/d2p/aibox`.

## Device Response

Device response envelope:

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

`body` is the business response JSON string; its field structure is consistent with the HTTP API response.

## Delivery and Compatibility

Registration publish ACK timeout is 5000 ms; a failed attempt is retried after 10 seconds while connection remains enabled. Heartbeats run every 30 seconds with a 2000 ms publish ACK timeout; 5 consecutive failures trigger reconnection.

Registration and heartbeat use object-valued `body`; business requests and responses use a JSON string. The current device publishes `register` and `heartbeat` respectively (the packaged HTML's upstream `response` examples are outdated). Registration and heartbeat completion are based on QoS 1 publish acknowledgements, not the platform's application-level `resCode`. The platform reply formats above are retained for integration compatibility. See `MqttLifecycleServiceImpl.cc`, `Register()` and `HeartBeat()`.

## Common Business API Reference

The following 29 entries cover the business interface sections in the packaged HTML. `Path` becomes MQTT `head.action`; `Method: POST` describes the HTTP equivalent, not an MQTT verb. **B07 requires HTTP and cannot be invoked through MQTT.** Response examples show the main fields; consult [API Fields](api-fields.md) for common response metadata.

The "Request Body" shown in this section represents the business JSON inside the outer MQTT `body` string. When actually sending via MQTT, execute `JSON.stringify` on the business JSON and place the result into the outer `body` field.

Channel-related APIs currently use `channelName`, `url`, and `videoChannelId`. Legacy fields `cameraName`, `rtspUrl`, and `cameraId` are not accepted by the current DTO.

The `resData.id` in the `Camera/Add` success response is the channel ID. Use this value in subsequent delete, update, snapshot, and task configuration APIs as `videoChannelId` or `channelId`.

### B01 - Create Channel

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/Add` |
| Method | POST |
| API Description | Create a live stream, VOD, ONVIF, local video, or USB channel. For live RTSP access, `channelName`, `url`, and `channelType` are required. |

Request Body

```json
{
  "channelType": 0,
  "channelName": "测试实时通道",
  "url": "rtsp://:@:554/h264/ch1/main/av_stream",
  "channelCode": "",
  "channelPic": ""
}
```

Full MQTT Request Example

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"id": "RT0000000002"}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelType` | number | Yes | `0` | Channel type: 0 live; 1 VOD; 2 ONVIF; 3 local video; 6 USB. |
| `channelName` | string | Recommended | `测试实时通道` | Channel name, used for display on the page. |
| `url` | string | Required for live stream | `rtsp://...` | Video stream URL. Legacy field `rtspUrl` is not accepted. |
| `channelCode` | string | No | `CAM-001` | External channel code; may be empty. |
| `channelPic` | string | No | `/data/pic/1.jpg` | Channel cover or snapshot path; may be empty. |

| Response Field | Type | Description |
| --- | --- | --- |
| `resCode` | number | 1 for success, 0 for failure. |
| `resMsg[].msgCode` | string | Business message code. |
| `resMsg[].msgText` | string | Business message text. |
| `resData.id` | string | New channel ID, e.g. `RT0000000002`. Use this value in subsequent APIs. |

### B02 - Delete Channel

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/Delete` |
| Method | POST |
| API Description | Delete a single video channel. Use `videoChannelId` here, not `cameraId`. |

Request Body

```json
{
  "videoChannelId": "RT0000000002"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | Yes | `RT0000000002` | Channel ID, from the `resData.id` of the create channel response or channel list. |

### B03 - Update Channel

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/Update` |
| Method | POST |
| API Description | Update the name, stream URL, type, and other information of an existing channel. |

Request Body

```json
{
  "videoChannelId": "RT0000000002",
  "channelType": 0,
  "channelName": "测试实时通道-修改",
  "url": "rtsp://:@:554/h264/ch1/main/av_stream",
  "channelCode": "CAM-001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | Yes | `RT0000000002` | Channel ID to update. |
| `channelType` | number | Yes | `0` | Channel type; same enumeration as create channel. |
| `channelName` | string | No | `测试实时通道-修改` | Channel name. |
| `url` | string | No | `rtsp://...` | Video stream URL. |
| `channelCode` | string | No | `CAM-001` | External channel code. |

### B04 - Paginated Channel Query

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/Page` |
| Method | POST |
| API Description | Paginated query for the video channel list; supports filtering by name and status. |

Request Body

```json
{
  "pageNum": 1,
  "pageSize": 10,
  "channelName": "测试",
  "channelStatus": 1
}
```

Response Body

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

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `pageNum` | number | Yes | `1` | Page number, starting from 1. |
| `pageSize` | number | Yes | `10` | Page size. |
| `channelName` | string | No | `测试` | Fuzzy search by channel name. |
| `channelStatus` | number | No | `1` | Channel status filter; use actual device enumeration values. |

### B05 - Batch Delete Channels

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/BatchDelete` |
| Method | POST |
| API Description | Batch delete multiple video channels. |

Request Body

```json
{
  "videoChannelIds": ["RT0000000002", "RT0000000003"]
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `videoChannelIds` | string[] | Yes | `["RT0000000002"]` | Array of channel IDs to delete. |

### B06 - Channel Snapshot

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Camera/GetPicture` |
| Method | POST |
| API Description | Capture a snapshot from the specified channel. |

Request Body

```json
{
  "videoChannelId": "RT0000000002"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"picUrl": "/data/picture/RT0000000002.jpg"}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `videoChannelId` | string | Yes | `RT0000000002` | Channel ID to capture snapshot from. |

### B07 - Create Local Video Channel

`POST /gtw/cwai/Camera/AddVideo`

This endpoint now requires authenticated HTTP and an upload session owned by that HTTP user. MQTT invocation is rejected by `MessageCameraHandler::Handle(..., context, ...)`. The packaged HTML's example with a device-local `filePath` is obsolete.

Upload the video with the HTTP chunked-upload API using `purpose: "video"`, then submit the completed `uploadId` using the same HTTP user. See [API Fields: Chunk Upload Fields](api-fields.md#chunk-upload-fields) and [API Overview](api.md).

HTTP request body:

```json
{"uploadId":"COMPLETED_UPLOAD_ID","channelName":"Offline video"}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `uploadId` | string | Yes for this flow | Completed video upload session owned by the current HTTP user. |
| `channelName` | string | Recommended | Display name of the new channel. |
| `channelCode` | string | No | External channel code. |

`contentLength`, `fileName`, and the actual local path are resolved by the server from the uploaded file; do not supply arbitrary server paths. A successful response contains the channel ID:

```json
{"resCode":1,"resMsg":[],"resData":{"id":"VD0000000001"}}
```

### C01 - Save and Enable Task Configuration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/SaveOrUpdate` |
| Method | POST |
| API Description | Save algorithm task configuration for a specified channel and enable or update the task. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID, from channel creation or list query. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID, from algorithm list or channel available algorithms API. |
| `scheduleId` | string | No | — | Schedule template ID; pass an empty string or omit if not using a template. |
| `taskConfig` | object | Yes | `{"params":[]}` | Algorithm runtime configuration; structure determined by algorithm metadata. |
| `taskConfig.params` | object[] | No | `[{"key":"alarmInterval","value":"60"}]` | Algorithm parameter list. |
| `taskConfig.areas` | object[] | No | `[]` | Detection region list. |
| `taskConfig.shieldedAreas` | object[] | No | `[]` | Shielded region list. |
| `taskConfig.facesetConfig` | object[] | No | `[]` | Face library related configuration. |

### C02 - Modify Task Parameters

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/ModifyParam` |
| Method | POST |
| API Description | Modify algorithm parameters of a configured task. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |
| `taskConfig` | object | Yes | `{"params":[]}` | New task configuration. |

### C03 - Query Task Parameters

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/QueryParam` |
| Method | POST |
| API Description | Query current task configuration for a specified channel and algorithm. |

Request Body

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"taskConfig": {"params": [], "areas": [], "shieldedAreas": [], "facesetConfig": []}}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |

### C04 - Start/Stop Task

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/SwitchTask` |
| Method | POST |
| API Description | Enable or stop an algorithm task on a specified channel. |

Request Body

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001",
  "switch": 1
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |
| `switch` | number | Yes | `1` | 1 to enable, 0 to stop. |

### C05 - Delete Task Configuration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/Delete` |
| Method | POST |
| API Description | Delete task configuration for a specified channel and algorithm. |

Request Body

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |

### C06 - Query Available Algorithms for Channel

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/SelectAllAlgorithmInfo` |
| Method | POST |
| API Description | Query the list of algorithms that can be configured for a specified channel. |

Request Body

```json
{
  "channelId": "RT0000000002"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": [{"algorithmId": "ALG0000000001", "algorithmName": "安全帽检测"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |

### C07 - Query Algorithm Configuration Details

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Task/SelectConfigByAlgorithmId` |
| Method | POST |
| API Description | Query configuration details for a specified channel and algorithm. |

Request Body

```json
{
  "channelId": "RT0000000002",
  "algorithmId": "ALG0000000001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"algorithmId": "ALG0000000001", "taskConfig": {"params": []}}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `channelId` | string | Yes | `RT0000000002` | Channel ID. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |

### D01 - Create Core Video Task

| Field | Value |
| --- | --- |
| Path | `/v1/cwai/aihost/TaskCreate` |
| Method | POST |
| API Description | Create a video analysis task on the AI core, typically sent by the platform or business layer based on channel and algorithm configuration. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `taskId` | string | Yes | `task-RT0000000002-ALG0000000001` | Unique task ID. |
| `videoChannelId` | string | Yes | `RT0000000002` | Video channel ID. |
| `videoChannelName` | string | No | `测试实时通道` | Video channel name. |
| `streamUrl` | string | No | `rtsp://...` | Video stream URL. |
| `algorithmCode` | string | Yes | `helmet_detect` | Algorithm code. |
| `algorithmUpdateTime` | string | Yes | `1716172800000` | Algorithm update time or version timestamp; fill in from algorithm package metadata. |
| `algorithmId` | string | No | `ALG0000000001` | Platform algorithm ID. |
| `algorithmName` | string | No | `安全帽检测` | Algorithm name. |
| `taskConfig` | object | No | `{"params":[]}` | Algorithm runtime configuration. |

### D02 - Cancel Core Video Task

| Field | Value |
| --- | --- |
| Path | `/v1/cwai/aihost/TaskCancle` |
| Method | POST |
| API Description | Cancel a specified core video task. |

Request Body

```json
{
  "taskId": "task-RT0000000002-ALG0000000001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `taskId` | string | Yes | `task-RT0000000002-ALG0000000001` | Task ID to cancel. |

### D03 - Query Core Information

| Field | Value |
| --- | --- |
| Path | `/v1/cwai/aihost/Info` |
| Method | POST |
| API Description | Query AI core runtime information. |

Request Body

```json
{
  "devId": ""
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"devId": "", "hostStatus": 0}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `devId` | string | Yes | — | Device SN or device ID. |

### E01 - Query MQTT Parameters

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/System/QueryMqttAdapterParam` |
| Method | POST |
| API Description | Query device MQTT adapter parameters. |

Request Body

```json
{}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"switch": true, "url": "192.168.0.10", "port": 1883, "authMode": 0}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| No request parameters; send an empty object `{}`. |

### E02 - Set MQTT Parameters

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/System/SetMqttAdapterParam` |
| Method | POST |
| API Description | Set device MQTT connection parameters. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `switch` | boolean | Yes | `true` | Whether to enable MQTT adapter. |
| `url` | string | Yes | `192.168.0.10` | MQTT Broker address. |
| `port` | number | Yes | `1883` | MQTT Broker port. |
| `authMode` | number | Yes | `0` | Authentication mode; fill in per device configuration enumeration. |
| `clientId` | string | No | — | Client ID. |
| `userName` | string | No | `aibox::` | MQTT username. |
| `passwd` | string | No | `******` | MQTT password. |

### E03 - Query/Modify Run Mode

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/System/QueryRunModeParam`, `/gtw/cwai/System/ModifyRunModeParam` |
| Method | POST |
| API Description | Query or modify device run mode. |

Query Request Body

```json
{}
```

Modify Request Body

```json
{
  "runMode": 1
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `runMode` | number | Required for modification | `1` | Run mode value; fill in per device supported enumeration. |

### E04 - Query/Modify IoT Network Parameters

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/System/QueryIotNetworkParam`, `/gtw/cwai/System/ModifyIotNetworkParam` |
| Method | POST |
| API Description | Query or modify IoT network parameters. |

Query Request Body

```json
{}
```

Modify Request Body

```json
{
  "mqttIp": "192.168.0.10",
  "mqttPort": 1883,
  "httpUrl": "http://192.168.0.10:18080",
  "status": true
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `mqttIp` | string | Required for modification | `192.168.0.10` | MQTT service address. |
| `mqttPort` | number | Required for modification | `1883` | MQTT service port. |
| `httpUrl` | string | Required for modification | `http://192.168.0.10:18080` | HTTP service address. |
| `status` | boolean | Required for modification | `true` | Whether to enable this network configuration. |

### F01 - Paginated Algorithm Query

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Page` |
| Method | POST |
| API Description | Paginated query for the algorithm list. |

Request Body

```json
{
  "pageNum": 1,
  "pageSize": 10,
  "algorithmName": "安全帽",
  "algorithmId": ""
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"total": 1, "list": [{"algorithmId": "ALG0000000001", "algorithmName": "安全帽检测"}]}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `pageNum` | number | Yes | `1` | Page number, starting from 1. |
| `pageSize` | number | Yes | `10` | Page size. |
| `algorithmName` | string | No | `安全帽` | Filter by algorithm name. |
| `algorithmId` | string | No | `ALG0000000001` | Filter by algorithm ID. |

### F02 - Add Algorithm Configuration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Add` |
| Method | POST |
| API Description | Add a new algorithm configuration record. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"algorithmId": "ALG0000000001"}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `algorithmCode` | string | No | `helmet_detect` | Algorithm code. |
| `algorithmName` | string | Yes | `安全帽检测` | Algorithm name. |
| `algorithmCategory` | number | Yes | `1` | Algorithm category; fill in per platform enumeration. |
| `algorithmUsage` | number | Yes | `0` | Algorithm usage; fill in per platform enumeration. |
| `checkType` | number | No | `0` | Detection type. |
| `remark` | string | No | `工地安全检测` | Remark. |
| `eventType` | string | No | `helmet_alarm` | Event type code. |
| `filePath` | string | No | `/data/algorithm/helmet.zip` | Algorithm file path. |

### F03 - Edit Algorithm Configuration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Update` |
| Method | POST |
| API Description | Edit an existing algorithm configuration. |

Request Body

```json
{
  "algorithmId": "ALG0000000001",
  "algorithmName": "安全帽检测-新版",
  "algorithmCategory": 1,
  "remark": "更新参数"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |
| `algorithmName` | string | No | `安全帽检测-新版` | Algorithm name. |
| `algorithmCategory` | number | No | `1` | Algorithm category. |
| `remark` | string | No | `更新参数` | Remark. |

### F04 - Delete Algorithm Configuration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/Algorithm/Delete` |
| Method | POST |
| API Description | Delete a specified algorithm configuration. |

Request Body

```json
{
  "algorithmId": "ALG0000000001"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |

### F05 - Save Algorithm Orchestration

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/save` |
| Method | POST |
| API Description | Save orchestration algorithm version and metadata. |

Request Body

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

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}]
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `confVersionId` | string | Yes | `CONF0000000001` | Orchestration configuration version ID. |
| `configVersionName` | string | No | `安全帽编排-v1` | Orchestration configuration version name. |
| `algorithmId` | string | Yes | `ALG0000000001` | Algorithm ID. |
| `algorithmCategory` | string | No | `1` | Algorithm category. |
| `algorithmUsage` | string | No | `0` | Algorithm usage. |
| `remark` | string | No | `编排测试` | Remark. |
| `atomicList` | string | No | `[]` | Atomic capability list; current DTO is a string — pass a JSON string. |
| `algorithmProcessdata` | string | No | `{}` | Algorithm process data; current DTO is a string — pass a JSON string. |
| `algorithmMetadata` | string | No | `{}` | Algorithm metadata; current DTO is a string — pass a JSON string. |
| `filePath` | string | No | `/data/algorithm/layout/helmet.json` | Orchestration file path. |

### F06 - Query Orchestration Algorithm List

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/list` |
| Method | POST |
| API Description | Query the orchestration algorithm list. |

Request Body

```json
{
  "supplier": "CWAI",
  "algorithmUsage": -1,
  "filePath": ""
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": []
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `supplier` | string | No | `CWAI` | Supplier identifier. |
| `algorithmUsage` | number | No | `-1` | Algorithm usage filter; -1 means no filter. |
| `filePath` | string | No | — | File path filter. |

### F07 - Query Orchestration Algorithm Details

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/algorithm/layout/detail` |
| Method | POST |
| API Description | Query details of a specified orchestration algorithm. |

Request Body

```json
{
  "id": "ALG0000000001",
  "filePath": "/data/algorithm/layout/helmet.json"
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": {"id": "ALG0000000001"}
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `id` | string | Yes | `ALG0000000001` | Orchestration algorithm ID. |
| `filePath` | string | No | `/data/algorithm/layout/helmet.json` | Orchestration file path. |

### F08 - Query Atomic Action List

| Field | Value |
| --- | --- |
| Path | `/gtw/cwai/atomic/action/list` |
| Method | POST |
| API Description | Query the atomic action list. |

Request Body

```json
{
  "actionUsage": 0,
  "filePath": ""
}
```

Response Body

```json
{
  "resCode": 1,
  "resMsg": [{"msgCode": "0", "msgText": "操作成功"}],
  "resData": []
}
```

| Request Field | Type | Required | Example | Remarks |
| --- | --- | --- | --- | --- |
| `actionUsage` | number | No | `0` | Action usage; fill in per platform enumeration. |
| `filePath` | string | No | — | Action definition file path. |

## Common Business Actions

| Module | Action Examples |
| --- | --- |
| Core Task | `/v1/cwai/aihost/TaskCreate`、`/v1/cwai/aihost/TaskCancle`、`/v1/cwai/aihost/Info` |
| System | `/gtw/cwai/System/QueryDeviceInfo`、`/gtw/cwai/System/QueryHardwareResource`、`/gtw/cwai/System/QueryMqttAdapterParam`、`/gtw/cwai/System/SetMqttAdapterParam` |
| Task Configuration | `/gtw/cwai/Task/ModifyParam`、`/gtw/cwai/Task/QueryParam`、`/gtw/cwai/Task/SwitchTask`、`/gtw/cwai/Task/SaveOrUpdate`、`/gtw/cwai/Task/Delete` |
| Camera | `/gtw/cwai/Camera/Add`、`/gtw/cwai/Camera/Update`、`/gtw/cwai/Camera/Page`、`/gtw/cwai/Camera/Delete`、`/gtw/cwai/Camera/GetPicture` |
| Algorithm | `/gtw/cwai/Algorithm/Page`、`/gtw/cwai/Algorithm/Add`、`/gtw/cwai/Algorithm/Update`、`/gtw/cwai/Algorithm/Delete`、`/gtw/cwai/algorithm/layout/save` |
