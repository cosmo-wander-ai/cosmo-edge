---
title: MQTT Reference
description: MQTT topics, connection parameters, message envelope, registration, heartbeat, platform requests, and device responses.
prev:
  text: API Fields
  link: /en/reference/api-fields
next:
  text: HTTP Webhook Reference
  link: /en/reference/webhook
---

# MQTT Reference

CosmoEdge can integrate with an external platform through MQTT topics for registration, heartbeat, platform-to-device requests, and device responses.

## Topics

| Direction | Topic | Purpose |
| --- | --- | --- |
| Device -> Platform | `/d2p/aibox` | Registration and business responses. |
| Device -> Platform | `/d2p/aibox/heartbeat` | Device heartbeat. |
| Platform -> Device | `/p2d/aibox/{deviceSn}` | Platform requests to a device. |
| Platform -> Device | `/p2d/aibox/heartbeat/{deviceSn}` | Heartbeat-related downstream messages. |

## Connection Parameters

System configuration fields are summarized in [API Fields](api-fields.md#mqtt-parameters).

The current implementation supports built-in IoT authentication and normal username/password authentication. Do not publish real usernames, passwords, broker addresses, or device serial numbers.

## Message Envelope

```json
{
  "head": {
    "requestId": "REQUEST_ID",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "request"
  },
  "body": "{}"
}
```

`body` is commonly a JSON string containing the business request or response payload.

## Platform Requests and Device Responses

A device should process platform requests only when `head.deviceSn` matches the local device and the message type is valid. Device responses are published back to the platform with the same request correlation ID when applicable.
