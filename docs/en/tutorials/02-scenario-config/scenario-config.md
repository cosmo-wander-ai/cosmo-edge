---
title: 'Volume 2: Scenario Configuration'
description: Configure video analytics scenarios, assign algorithms, draw regions, tune parameters, and review alarms.
prev:
  text: 'Volume 1: Quick Start'
  link: /en/tutorials/01-quickstart/quickstart
next:
  text: 'Volume 3: VLM / DINO Guide'
  link: /en/tutorials/03-vlm-guide/vlm-guide
---

# Volume 2: Scenario Configuration

This tutorial explains how to configure built-in video analytics scenarios after the device and web console are running.

## Learning Path

1. Prepare a video channel.
2. Assign an algorithm to the channel.
3. Configure detection regions.
4. Tune thresholds and key parameters.
5. Enable visualization.
6. Review and export alarms.

## Chapter 1: Safety Helmet Detection

Use safety helmet detection as the first scenario because it covers the common workflow: video source, algorithm binding, region configuration, threshold tuning, and alarm review.

### 1.1 Prepare a Video Channel

Create or select a video channel. Confirm that live video can be displayed before assigning algorithms.

### 1.2 Assign an Algorithm

Bind the helmet-related algorithm/template to the channel. Confirm the algorithm is enabled and the task status is normal.

### 1.3 Configure Detection Regions

Draw the region where detection should apply. Keep the region close to the actual work area to reduce false positives.

### 1.4 Tune Key Parameters

Typical parameters include confidence threshold, category selection, region rules, sensitivity, and event debounce behavior. Start with defaults, then tune one parameter at a time.

### 1.5 Algorithm Visualization

Enable visual overlays to confirm that the detected objects and regions match your expectations.

### 1.6 Review Alarms

Open the alarm/event page to verify that events are generated with correct time, channel, task, category, and snapshot information.

## Chapter 2: Practice with Another Scenario

Repeat the same workflow with a different scenario. Focus on parameter differences and validation method rather than memorizing UI positions.

## Next Step

Continue with [Volume 3: VLM / DINO Guide](../03-vlm-guide/vlm-guide.md).
