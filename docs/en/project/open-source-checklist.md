---
title: Open Source Checklist
description: Repository, build, license, resource, sensitive-data, documentation, and release checks before publishing.
prev:
  text: CI and Quality Checks
  link: /en/development/ci
next:
  text: Sensitive Data Review
  link: /en/project/sensitive-data-review
---

# Open Source Checklist

Use this checklist before making the repository public.

## Repository Basics

- [x] Root README prepared in English.
- [x] Chinese README prepared.
- [x] Apache 2.0 license added.
- [x] NOTICE file added.
- [x] Contributing, security, code of conduct, issue templates, and PR template added.
- [ ] Final public repository URL confirmed.
- [ ] Final logo/social preview assets confirmed.

## Build and Runtime

- [x] Documentation build path verified.
- [ ] x86 Docker path verified on a clean machine.
- [ ] Sophon package build verified on the release environment.
- [ ] CPU test build verified for the release commit.

## License

- [x] Main repository license set to Apache 2.0.
- [ ] Third-party source and binary license audit completed.
- [ ] Prebuilt binary redistribution rights confirmed.
- [ ] Model/resource licenses confirmed.

## Sensitive Data

- [x] Candidate scan script added: `scripts/open_source_scan.ps1`.
- [ ] Scan findings reviewed and documented.
- [ ] Real tokens, passwords, private keys, customer names, device serial numbers, private IPs, and private domains removed.

## Documentation

- [x] Chinese documentation site added.
- [x] English documentation site added.
- [ ] Public screenshots reviewed for sensitive OCR content.
- [ ] Benchmark claims tied to reproducible reports.

## Suggested Release Order

1. Finish sensitive-data review.
2. Finish license/resource audit.
3. Verify build and runtime on a clean machine.
4. Publish the repository.
5. Publish release notes and public validation reports.
