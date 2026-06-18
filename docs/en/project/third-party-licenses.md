---
title: Third-Party Licenses
description: Third-party source, codec, binary, model/resource, frontend, documentation, Docker, and NOTICE audit guidance.
prev:
  text: Security Notes
  link: /en/project/security
next:
  text: Release Notes
  link: /en/project/release-notes
---

# Third-Party Licenses

This page tracks license review work required before the repository is published.

## Dependency Sources

Review dependencies from vendored C/C++ code under `3rd/`, prebuilt binaries, frontend npm dependencies, documentation-site npm dependencies, Docker images, system packages, model weights, labels, templates, and sample media.

## C/C++ Runtime Dependencies

Maintain an audit table with dependency name, version, license, source URL, whether it is redistributed, and required NOTICE text.

## Codec and GPL Risk

Codec-related dependencies require careful review. Confirm whether binaries are dynamically linked, redistributed, or optional, and whether patent/licensing obligations apply.

## Prebuilt Binary Review

Every prebuilt `.so`, `.dll`, `.lib`, or SDK binary needs a redistribution conclusion before release.

## Models and Resources

Model weights and resource packages are not automatically covered by the repository license. Track each model/resource license independently.

## Pre-Release Actions

- [ ] Complete third-party inventory.
- [ ] Confirm binary redistribution rights.
- [ ] Confirm model/resource licenses.
- [ ] Update `NOTICE`.
- [ ] Document unresolved items in the release checklist.
