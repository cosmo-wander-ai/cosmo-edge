---
title: Public Validation Report
description: Public validation status, README claim mapping, report structure, sensitive exclusions, and minimum publishable version.
prev:
  text: Repository Metadata
  link: /en/project/repository-metadata
next:
  text: Benchmark Reproduction Guide
  link: /en/project/benchmarks
---

# Public Validation Report

This page defines how public validation claims should be supported.

## Validation Status

README badges and performance claims must be backed by reproducible reports. If raw datasets or videos cannot be published, publish methodology, environment, metrics, and limitations instead.

## README Claim Mapping

Each public claim should map to test environment, commit ID, dataset or sample description, metric definition, result table, and known limitations.

## Suggested Report Structure

1. Environment.
2. Hardware and runtime package.
3. Model/resource version.
4. Input videos or public substitute dataset.
5. Scenario configuration.
6. Metrics and measurement method.
7. Results.
8. Limitations.

## Must Not Include

Private videos, customer names, exact private deployment locations, tokens, passwords, private IPs, device serial numbers, or proprietary model weights without permission.

## Minimum Publishable Version

At minimum, publish test methodology and clearly mark unverified values as TODO. Do not present unverified benchmark claims as final results.
