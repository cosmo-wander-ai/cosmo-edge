---
title: API Overview
description: API entry points, categories, authentication notes, WebSocket, and packaged API documents.
prev:
  text: Architecture Overview
  link: /en/guide/architecture
next:
  text: API Fields
  link: /en/reference/api-fields
---

# API Overview

This page summarizes API entry points that can be verified from the current source tree. For field-level details, continue with [API Fields](api-fields.md), [MQTT Reference](mqtt.md), and [HTTP Webhook Reference](webhook.md).

## Route Entry Points

The main management APIs use `/gtw/cwai/...`. Core AI Host APIs use `/v1/cwai/aihost/...` and selected compatibility routes under `/gtw/cwai/aihost/...`.

## API Categories

| Category | Typical Scope |
| --- | --- |
| System | Device information, network settings, runtime configuration. |
| Media | Video source management, stream preview, recording-related operations. |
| Task and scenario | Scenario task creation, configuration, status, and event queries. |
| Model/resource | Model repository, algorithm templates, labels, and resource metadata. |
| Integration | MQTT, HTTP push, WebSocket, and platform integration settings. |

## Authentication

The source contains routes marked as authenticated and unauthenticated. HTTP requests use an `mtk` token check. MQTT-dispatched internal requests are routed through the API router differently and should be reviewed when documenting security boundaries.

TODO before public release:

- Confirm token location.
- Confirm token expiration behavior.
- Confirm error codes for authentication failures.

## WebSocket

WebSocket is used for live runtime status and browser-facing updates. Keep endpoint names synchronized with the frontend implementation before publication.

## Packaged API HTML

The repository contains packaged/static API references under `data/Interface` and web static files. These are useful references, but Markdown docs should be treated as the open-source source of truth after review.
