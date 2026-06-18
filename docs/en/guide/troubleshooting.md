---
title: Troubleshooting
description: Common runtime, build, port, package, and documentation-site issues.
prev:
  text: Runtime Configuration
  link: /en/guide/configuration
next:
  text: Architecture Overview
  link: /en/guide/architecture
---

# Troubleshooting

## Web Console Cannot Open

Check whether nginx and the backend process are running, and whether the configured HTTP port is listening.

```bash
ps aux | grep -E 'nginx|cosmo-engine|srs'
```

## Port Conflicts

If startup fails, verify that runtime ports are not already used by another process. Update deployment configuration and restart the services.

## No Release Package Generated

If the expected package directory is missing, confirm that the packaging script completed successfully and that required SDK/resource paths exist.

## Sophon Base Image Missing

If a Sophon build references a private or unavailable image, replace it with a public base image or document the required private setup outside the open-source repository.

## Restricted Backend Mode

If the backend starts with limited capabilities, check device validation, model guard availability, resource paths, and license-sensitive prebuilt files.

## Documentation Build Fails

```bash
npm ci
npm run docs:build
```

If PowerShell blocks `npm.ps1` on Windows, use:

```powershell
npm.cmd run docs:build
```
