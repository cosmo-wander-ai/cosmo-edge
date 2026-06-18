# Web Frontend Dependency Audit Notes

**Date:** 2026-05-30
**Branch:** `chore/deps-audit-2026-05`
**Final residual:** 4 high, 0 moderate

This document records the residual `npm audit` high-severity advisories
with a written-down "no-impact" justification for each, per the
project's Definition B for "prod 0 high".

## Definition

"prod 0 high" follows **Definition B**: every `npm audit` high must have
a written-down "no-impact" justification (API-level zero hit, dev-only
transitive, or vendored bypass). This document is that justification.

## Audit upgrade line summary

| Step | Action | Outcome |
|---|---|---|
| 1 | `uuid` 13.0.0 → 13.0.2 | Established true baseline (5 high after clean `npm ci`) |
| 1.5 | Add `.npmrc` + fix uuid lock URL | Registry locked to npmmirror |
| 1.6 | Drop `yarn.lock` | Engineering debt cleared |
| 2 | `vue` 3.5.29 → 3.5.35 + transitive | `postcss` moderate auto-resolved |
| 3 | `sass` + `sass-embedded` 1.97.3 → 1.100.0 | `immutable` high auto-resolved |
| 4 | `lodash` — accept, no upgrade | API-level 0 hit (see below) |
| 5 | `element-plus` 2.13.2 → 2.14.1 | **Deferred to post-Phase-2** |
| 5.5 | This document | Audit closeout |

Removed from original plan (not on residual-high list after `npm ci`
clean baseline): `dagre`, `tree-transfer-vue3` vendor migration.

## Residual high advisories

### lodash (prod direct)

- Advisories: GHSA-r5fr-rjxr-66jc (`_.template` code injection),
  GHSA-f23m-r3pf-42rh (`_.unset` / `_.omit` prototype pollution)
- **Impact: none** — project code does not call any affected API.
- Re-verify:
  ```bash
  grep -rEn "_\.template\b|lodash\.template|require.*lodash/template" src/
  grep -rEn "_\.(unset|omit)\b|lodash\.(unset|omit)\b|require.*lodash/(unset|omit)\b|from ['\"]lodash/(unset|omit)['\"]" src/web/src/
  ```
  Both expected to return zero hits.

### lodash-es (prod, transitive via element-plus)

- Same advisories as `lodash` (shared codebase, ES-module build).
- **Impact: none** — the grep above also covers ES-module imports;
  project does not import `lodash-es/template`, `lodash-es/unset`,
  or `lodash-es/omit`.
- Element-plus 2.14.x (deferred upgrade) may bump this to a fixed
  version transitively.

### picomatch (dev only)

- Advisories: GHSA-3v7f-55p6-f55p (method injection),
  GHSA-c2c7-rcm5-vvqj (ReDoS)
- **Impact: none** — picomatch is transitive via `sass` (through
  `@parcel/watcher`) and `vite` (through `fdir` / `tinyglobby`).
  Both parents are build-time tools; picomatch is not bundled into
  the production output.
- Re-verify:
  ```bash
  cd src/web && npm ls picomatch --all
  ```
  Expected: only `sass` / `vite` branches, no prod runtime branch.

### vite (dev only)

- Advisories (5): GHSA-g4jq-h2w9-997c, GHSA-jqfw-vq24-v9c3,
  GHSA-93m4-6634-74q7, GHSA-4w7w-66w2-5vf9, GHSA-p9ff-h696-f583
- **Impact: none on production** — all five advisories concern Vite's
  dev server (file serving, fs.deny bypass, sourcemap path traversal,
  WebSocket arbitrary file read). Production output is built by
  `vite build` and served as static files; the dev server is never
  deployed.
- Dev environment exposure: developer machines only, not internet-facing.
- Vite 6.4.2+ contains fixes; minor upgrade is queued separately from
  the original "Vite 6 → 8 major" backlog item.

## Workflow notes

- Project pins npm registry to `npmmirror` via `src/web/.npmrc`.
- `npm audit` must be run with `--registry=https://registry.npmjs.org`
  because npmmirror does not implement the `/-/npm/v1/security/*` API.
- `yarn.lock` was removed; do not run `yarn install` in this directory.

## Re-verification cadence

Re-run the full audit after any of:
- Upgrade of `lodash`, `lodash-es`, `element-plus`, `vite`, `sass`,
  or `sass-embedded`
- Addition of new direct dependencies in `src/web/package.json`
- Introduction of new `lodash` / `lodash-es` API usage
  (re-run the greps under "lodash" above)

Command:
```bash
cd src/web && npm audit --registry=https://registry.npmjs.org --cache /tmp/cosmo-npm-cache
```

Expected as of this commit: **4 high (lodash / lodash-es / picomatch / vite),
0 moderate**.
