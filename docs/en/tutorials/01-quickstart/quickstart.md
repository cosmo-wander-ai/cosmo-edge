---
title: 'Volume 1: Quick Start'
description: First boot, hardware connection, web console access, live preview, and statistics.
prev:
  text: Tutorials
  link: /en/tutorials/
next:
  text: 'Volume 2: Scenario Configuration'
  link: /en/tutorials/02-scenario-config/scenario-config
---

# Volume 1: Quick Start

This tutorial helps a first-time user connect a CosmoEdge device, open the management console, check live AI output, and view statistics.

## Hardware Introduction

A typical setup includes an edge device, power supply, network connection, one or more camera/video sources, and a browser on the same reachable network.

## Step 1: Hardware Connection

1. Connect power and network.
2. Connect cameras or make sure RTSP/video sources are reachable.
3. Confirm the device IP address from your router, DHCP server, discovery tool, or deployment notes.
4. Open the web console in a browser.

Use documentation-safe example addresses in public docs, such as `http://192.0.2.10`.

## Step 2: Access the Management Console

After logging in, review device information, network configuration, storage/runtime paths, service status, time settings, and model/resource availability.

Replace default credentials before production use.

## Step 3: View Real-Time AI Results

Open the live preview page and check whether the selected channel displays video, OSD overlays, algorithm results, and event status.

If the page is blank, verify the video source URL, decoder/runtime process status, browser network access, backend logs, media service logs, and whether the scenario task is enabled.

## Step 4: View Statistics

Use the statistics/event pages to confirm that the runtime is generating structured output, not only visual overlays.

Check event counts, timestamps, channel/task identity, snapshots, and export behavior if enabled.

## Completion Criteria

- The web console opens.
- At least one video source is visible.
- AI overlays or results appear.
- Events/statistics can be viewed.
- No private credentials or camera URLs remain in shared screenshots.

## Next Step

Continue with [Volume 2: Scenario Configuration](../02-scenario-config/scenario-config.md).
