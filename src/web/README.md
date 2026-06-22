# Web Frontend

Package manager: **npm**.

This directory is managed exclusively with `npm`. The previous `yarn.lock`
was removed in the dependency audit upgrade line of 2026-05 (see git history).
Do not run `yarn install` here — it will create a stale lockfile that npm
will not maintain.

For registry configuration see `.npmrc` in this directory.
