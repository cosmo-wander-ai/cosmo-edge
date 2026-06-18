---
title: Release Notes
description: Draft release notes, mandatory pre-release checks, known limitations, and related release documents.
prev:
  text: Third-Party Licenses
  link: /en/project/third-party-licenses
next:
  text: Repository Metadata
  link: /en/project/repository-metadata
---

# Release Notes

## Current Version

The initial open-source release is planned as `v0.1.0` unless maintainers choose another version.

## Completed

- Open-source README and Chinese README prepared.
- VitePress documentation site added.
- English documentation route added under `/en/`.
- Apache 2.0 license, NOTICE, contributing, security, code of conduct, issue templates, and PR template added.
- Sensitive-data scan script added.

## Must Confirm Before Release

- Public repository URL.
- Final license/resource audit.
- Which model/resource files are included.
- Public benchmark report and reproducible test environment.
- Public screenshots and OCR-sensitive material.
- Build and package verification on clean environments.

## Release Checklist

- [ ] `npm ci && npm run docs:build`
- [ ] x86 Docker smoke test
- [ ] Sophon package build
- [ ] CPU build/test path
- [ ] sensitive-data scan and manual review
- [ ] third-party license audit
- [ ] release notes finalized

## Related Documents

- [Open Source Checklist](open-source-checklist.md)
- [Public Validation Report](validation.md)
- [Benchmark Reproduction Guide](benchmarks.md)
- [Third-Party Licenses](third-party-licenses.md)
