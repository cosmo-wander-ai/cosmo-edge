## Summary

Describe what changed and why. For a fix, include the failure path or a before/after example when useful.

## Verification

### Common validation examples

| Change | Example |
| --- | --- |
| Documentation | `npm ci` → `npm run docs:build` |
| Frontend | `cd src/web` → `npm ci` → `npm run build` |
| C++ tests | `bash scripts/format_check.sh --staged --check` → `bash scripts/build_cpu_test.sh` → `./build_cpu/cosmo-tests` |
| x86 Docker smoke | `docker compose -f docker-compose.x86.yml up -d --build` |

Please list the commands you actually ran and their results.

List the relevant commands and checks you ran, their results, and any checks you could not complete. Include a parent baseline only when needed to identify a regression.

## Impact

Describe any effect on APIs, configuration, models, deployment, documentation, or third-party materials. State "None" if there is no such effect.

## Related issue (optional)

Link an issue if one already tracks this change. Remove this section otherwise.

## Delivery evidence (for package or device delivery)

Remove this section when the PR does not deliver a package or change a device.

- Source commit and tree:
- Target chip and build profile:
- Package SHA-256 and contents verification:
- Device validation, backup, and recovery status (if applicable):
- Candidate-bound acceptance (if requested):
