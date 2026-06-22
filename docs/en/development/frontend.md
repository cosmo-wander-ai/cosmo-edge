---
title: Frontend Development
description: Frontend stack, scripts, API directories, and menu entry notes.
prev:
  text: Models and Resources
  link: /en/reference/models
next:
  text: Backend Development
  link: /en/development/backend
---

# Frontend Development

The web console is a Vue-based frontend packaged with the CosmoEdge runtime. It provides model management, scenario configuration, live preview, event views, and system settings.

## Technology Stack

- Vue 3 based application structure.
- Vite-oriented frontend build tooling where applicable.
- Element Plus style component usage in existing pages.
- Internationalization files under the frontend source tree.

## Common Scripts

Check the frontend package files under `src/web/` for exact commands. Keep root-level docs site commands separate from application frontend commands.

## API Directory

Frontend API wrappers should remain aligned with backend route names and field contracts. When documenting a new endpoint, update both the reference docs and the frontend integration notes.

## Menu Entries

New pages should follow existing route/menu patterns instead of introducing a parallel navigation style. Keep Chinese and English labels synchronized where the frontend supports both locales.
