---
title: Sensitive Data Review
description: Pre-release sensitive-data scan scope, script usage, review table, and release decision template.
prev:
  text: Open Source Checklist
  link: /en/project/open-source-checklist
next:
  text: Security Notes
  link: /en/project/security
---

# Sensitive Data Review

Automated scans only report candidates; maintainers must decide whether each file can be published.

## What to Check

| Category | Examples | Action |
| --- | --- | --- |
| Secrets | token, password, secret, API key, private key | Remove, replace, or move to environment variables. |
| Device data | Real device SN, authorization code, production device ID | Replace with placeholders such as `DEVICE_SN`. |
| Network data | Private IP, internal domain, VPN address, private download URL | Remove or replace with documentation-safe examples. |
| Customer data | Customer names, project names, locations, camera positions | Anonymize or remove. |
| Models/resources | Private weights, bmodel, ONNX, private links | Confirm license; remove if not publishable. |
| Images/videos | Real scene images, screenshots, OCR-sensitive content | Review, anonymize, replace, or remove. |
| Binaries | Prebuilt `.so`, vendor SDK, closed plugin | Confirm redistribution rights. |

## Scan Script

```powershell
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1 -MaxMatches 200
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1 -IncludeThirdParty
```

## Manual Review Paths

| Path | Review Focus | Conclusion |
| --- | --- | --- |
| `README.md` / `README.zh-CN.md` | URLs, contacts, claims, screenshots | TODO |
| `docs/` | Screenshots, OCR comments, example IPs, device SNs | TODO |
| `.github/` | Templates remind users to remove sensitive data | TODO |
| `scripts/` | Private paths, mirrors, accounts, tokens | TODO |
| `data/resource/*` | Models, templates, images, internal names | TODO |
| `prebuild/` | Binary redistribution rights | TODO |
| `3rd/` | Third-party licenses | TODO |

## Release Rule

If ownership or permission is unclear, leave the conclusion as TODO and keep the file or resource out of the first public release.
