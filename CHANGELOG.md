# Changelog

All notable public changes to CosmoEdge will be documented in this file.

This project follows a release-note style inspired by Keep a Changelog. Versioning policy should be finalized before the first public release.

## [Unreleased]

### Added

- Open-source README in English and Simplified Chinese.
- VitePress documentation site.
- Five tutorial sections for quick start, scenario configuration, VLM / DINO, pipeline orchestration, and model porting.
- Build, deployment, configuration, troubleshooting, architecture, API, model/resource, frontend, and backend documentation.
- Open-source release checklist.
- Security notes and top-level `SECURITY.md`.
- Issue templates and pull request template.
- Apache License 2.0 `LICENSE`.
- Initial `NOTICE`.
- Third-party dependency and license-audit draft.

### Changed

- Public quick-start command is aligned with the current x86 Docker flow:
  `docker compose -f docker-compose.x86.yml up -d --build`.
- Sophon package build documentation is aligned with current helper scripts:
  `scripts/build_sophon_package.sh` and `scripts/build_sophon_package.ps1`.

### Pending Before First Public Release

- Complete third-party license audit.
- Confirm model/resource redistribution policy.
- Confirm public benchmark and validation claims.
- Confirm Windows support scope.
- Confirm production authorization and restricted-mode wording.
- Complete required third-party attribution in `NOTICE`.

## [0.1.0] - Pending

Initial public release candidate.

[Unreleased]: https://github.com/cosmo-wander-ai/cosmo-edge/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v0.1.0
