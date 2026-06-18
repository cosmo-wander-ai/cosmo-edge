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

This page collects field-level conventions that are repeatedly used by HTTP, MQTT, and webhook integrations.

## Common Response

| Field | Type | Notes |
| --- | --- | --- |
| `code` | number/string | Business status code. Confirm final type per endpoint. |
| `msg` | string | Human-readable status or error message. |
| `data` | object/array | Response payload. |
| `requestId` | string | Request correlation ID when present. |

## Pagination and Time Range

| Field | Type | Notes |
| --- | --- | --- |
| `pageNo` / `page` | number | Page number. |
| `pageSize` | number | Page size. |
| `startTime` | string/number | Start of query range. |
| `endTime` | string/number | End of query range. |

## Event Webhook Payload

Typical event records include event ID, task/scenario identity, channel identity, algorithm identity, timestamp, object/category information, confidence, image/snapshot URL, and optional attributes.

## HTTP Push Parameters

| Field | Type | Notes |
| --- | --- | --- |
| `url` | string | Receiver endpoint. |
| `method` | string | Usually `POST`. |
| `headers` | object | Optional custom headers; remove secrets from examples. |
| `timeout` | number | Request timeout if configurable. |

## MQTT Parameters

| Field | Type | Notes |
| --- | --- | --- |
| `mqttIp` / `url` | string | Broker address. |
| `port` | number | Broker port. |
| `clientId` | string | MQTT client identity. |
| `userName` | string | MQTT username. |
| `passwd` | string | MQTT password or placeholder. |
| `authMode` | number/string | Authentication mode. |
