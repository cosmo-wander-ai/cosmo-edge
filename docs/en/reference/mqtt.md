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
| `body` | string | JSON string. For downstream requests, the business JSON must be `JSON.stringify`-d and placed in this field. |

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

| Field | Type | Description |
| --- | --- | --- |
| `body.devId` | string | Device ID; currently the device SN. |
| `body.supplier` | string | Supplier; defaults to `CWAI`. |
| `body.aiHostVersion` | string | AI Host version. |
| `body.engineType` | string | Engine type. |
| `body.deviceModel` | string | Device model. |
| `body.devType` | number | Device type; the box currently uses `2`. |

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
