---
title: HTTP Webhook Reference
description: HTTP event push configuration, payload structure, receiver recommendations, and packaged HTML reference notes.
prev:
  text: MQTT Reference
  link: /en/reference/mqtt
next:
  text: Models and Resources
  link: /en/reference/models
---

# HTTP Webhook Reference

CosmoEdge can push alarm/event data to an external HTTP receiver. The exact endpoint and configuration fields should be verified against the current backend before public release.

## Configuration API

Common configuration values include receiver URL, enable/disable switch, authentication headers, timeout, retry policy, and event filters. Do not publish real receiver URLs or tokens.

## Event Payload

```json
{
  "eventId": "EVENT_ID",
  "taskId": "TASK_ID",
  "channelId": "CHANNEL_ID",
  "eventType": "ALARM_TYPE",
  "timestamp": "2026-01-01T00:00:00Z",
  "objects": [],
  "attributes": {},
  "snapshotUrl": "https://example.com/snapshot.jpg"
}
```

Field details are summarized in [API Fields](api-fields.md#event-webhook-payload).

## Receiver Recommendations

- Accept `POST` requests with JSON payloads.
- Return a 2xx status code after the event is persisted or queued.
- Make receiver processing idempotent by event ID and request ID.
- Log payloads carefully and remove sensitive device/customer data from shared logs.
