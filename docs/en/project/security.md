---
title: Security Notes
description: Security boundaries, sensitive information, models/resources, ports, development mode, restricted mode, dependencies, and reporting.
prev:
  text: Sensitive Data Review
  link: /en/project/sensitive-data-review
next:
  text: Third-Party Licenses
  link: /en/project/third-party-licenses
---

# Security Notes

## Sensitive Information

Do not publish real tokens, passwords, private keys, certificates, customer names, project names, device serial numbers, internal domains, private IPs, private model links, or private camera URLs.

## Models and Resources

Model files, bmodel/ONNX artifacts, sample images, videos, and resource templates must be reviewed before release. If distribution permission is unclear, exclude the resource.

## Runtime Ports

Document exposed ports and expected network boundaries. Production deployments should restrict management interfaces to trusted networks.

## Development Mode

Development-only settings should not be presented as production defaults. Remove debug credentials, local-only shortcuts, and private mirrors from public examples.

## Restricted Mode

The backend may start with limited capabilities when validation or model guard checks fail. Document the behavior without exposing private validation logic.

## Third-Party Dependencies

Track security updates for C/C++ dependencies, frontend npm packages, Docker images, system packages, and prebuilt binaries.

## Vulnerability Reports

Use `SECURITY.md` for the current reporting process. Ask reporters to remove secrets, customer data, and private logs before sharing reproduction material.
