---
title: Frontend Development
description: Frontend stack, project structure, common scripts, API layer, i18n, build configuration, and how to add new pages.
prev:
  text: Contributor Guide
  link: /en/development/contributing
next:
  text: Backend Development
  link: /en/development/backend
---

# Frontend Development

The frontend project is located under `src/web/`. It is a Vue 3 + Vite single-page application that provides model management, scenario configuration, pipeline orchestration, live preview, alarm views, and system settings.

## Project Structure

```
src/web/
├── package.json              # Dependencies and scripts
├── vite.config.js            # Build config, aliases, proxy
├── scripts/                  # i18n validation & sync scripts
└── src/
    ├── main.js               # App entry point
    ├── App.vue               # Root component
    ├── api/                  # API modules
    │   └── index.js          # Merges all modules → global $API
    ├── assets/               # Images, icons, audio
    ├── components/           # Shared components
    ├── i18n/                 # vue-i18n setup, locales, short-scopes
    ├── router/
    │   └── index.js          # Hash-based router
    ├── styles/
    │   └── global.scss       # Global SCSS
    ├── utils/                # Axios wrapper, message, image preview, i18n loader, WebRTC player
    └── views/                # Page components
        ├── main/             # MainLayout (sidebar + header + menu)
        ├── home/             # Dashboard
        ├── box/              # Edge-device views (cameras, events, libraries, system, data-docking)
        └── gam/              # AI-management views (tasks, models, orchestration, image analysis)
```

## Technology Stack

| Category       | Library             | Version  | Purpose                           |
| -------------- | ------------------- | :------: | --------------------------------- |
| Framework      | Vue 3               | ^3.5.35  | Composition API + `<script setup>` |
| Build          | Vite                | 6.3.5    | Dev server, HMR, chunked production builds |
| Router         | Vue Router          | ^4.2.0   | Hash-based client-side routing    |
| UI             | Element Plus        | 2.13.2   | Component library (pinned)        |
| HTTP           | Axios               | ^1.7.0   | API client with interceptors      |
| Charts         | ECharts             | ^6.0.0   | Dashboard and statistics charts   |
| Flow Editor    | @vue-flow/core      | ^1.48.2  | Pipeline orchestration editor     |
|                | @vue-flow/background | ^1.3.2  | Flow editor background grid       |
|                | @vue-flow/controls  | ^1.1.3   | Flow editor zoom controls         |
|                | @vue-flow/minimap   | ^1.5.4   | Flow editor minimap               |
| Tree Transfer  | tree-transfer-vue3  | ^1.2.2   | Tree-shuttle component for algorithm selection |
| i18n           | Vue I18n            | ^9.14.5  | Internationalization              |
| Video          | flv.js              | ^1.6.2   | FLV stream playback               |
| WebRTC         | native RTCPeerConnection | —   | WHEP-based playback               |
| Layout         | dagre               | ^0.8.5   | Graph layout for flow diagrams    |
| Utilities      | lodash, moment, uuid, js-md5, mitt | — | Various utilities            |

## Getting Started

### Environment Variables

Create a `.env` or `.env.development` file under `src/web/`:

| Variable             | Purpose                                      |
| -------------------- | -------------------------------------------- |
| `VITE_APP_BASE_URL`  | App base path (default `/`)                  |
| `VITE_APP_API_URL`   | Backend API target for dev proxy             |

### Dev Server

```bash
cd src/web
npm install
npm run dev
```

The dev server starts on `http://localhost:3000`. All `/gtw`, `/event`, `/weblogo`, and `/web` requests are proxied to `VITE_APP_API_URL`.

### Production Build

```bash
cd src/web
npm run build
```

`prebuild` automatically runs `npm run i18n:check` before the build. If i18n checks fail, the build is blocked.

## Available Scripts

| Script                    | Purpose                                                  |
| ------------------------- | -------------------------------------------------------- |
| `npm run dev`             | Start Vite dev server                                    |
| `npm run build`           | Production build (via `prebuild` → i18n check)           |
| `npm run preview`         | Preview the production build locally                     |
| `npm run i18n:check`      | Run all 5 i18n validation scripts                        |
| `npm run resource-i18n:check` | Check resource i18n sync status                      |
| `npm run resource-i18n:sync`  | Sync resource i18n keys from the aiboxResource source (review diff before committing) |

The i18n validators cover: short-scope correctness, locale key consistency, glossary synchronization, dialog action button labels, and unused-key detection.

## Adding a New Page

### Step 1: Create the View Component

Create a new `.vue` file under `src/web/src/views/` in the appropriate subdirectory (`box/` for device-related views, `gam/` for AI-management views).

### Step 2: Register a Route

Add a route entry in `src/web/src/router/index.js`:

```js
{
  path: '/myModule/myPage',
  name: 'MyPage',
  component: () => import('@/views/myModule/myPage/index.vue')
}
```

Pages that need the sidebar + header layout should use the `MainLayout` parent route. Standalone pages (e.g. login, big-screen) do not require the layout wrapper.

Routes that require login are guarded by the global navigation guard, which checks `localStorage.getItem('token')`.

### Step 3: Add a Menu Entry

Edit `src/web/src/views/main/menu.js`, choose the matching `section` (`"core"`, `"display"`, `"task"`, `"resource"`, `"system"`), and add an entry:

```js
{
  index: '/myModule/myPage',
  titleKey: 'nav.myPage',
  icon: 'el-icon-xxx',
  section: 'task'
}
```

### Step 4: Add Translations

Add the new i18n keys to both of these files:
- `src/web/src/i18n/locales/zh-CN.js`
- `src/web/src/i18n/locales/en-US.js`

Menu labels go under `nav`; create a new top-level key for page-specific strings per module.

## API Layer

### Pattern

API modules live under `src/web/src/api/`. Each module default-exports an object of request functions.
`api/index.js` flattens those objects with object spread, and `main.js` injects the result as global `$API`.

Usage in components: `this.$API.dologin(params)` or `proxy.$API.dologin(params)` (Composition API).

### Existing Modules

| Module          | File           | Domain                                           |
| --------------- | -------------- | ------------------------------------------------ |
| Device          | `box.js`       | Login (`dologin`), cameras, events, system settings, audio, linkage |
| AI Management   | `gam.js`       | Algorithms, tasks, models, orchestration, image analysis |
| Algorithm Admin | `countManage.js` | Algorithms, orchestration, model configuration, license status         |
| Base Libraries  | `basePic.js`   | Face library, body library, item library, file imports |
| Live Stream     | `screen.js`    | Camera list, live stream lifecycle, WebSocket     |
| Onboarding      | `onboarding.js`| Guide status, completion, and reset                |

### Adding a New Endpoint

Add a function to the relevant module object. When creating a module file, spread its default export into `api/index.js`:

```js
// api/myModule.js
import { request } from '@/utils/request'

export default {
  queryMyData: data => request({
    url: '/gtw/cwai/MyModule/Query',
    method: 'post',
    data
  })
}

// api/index.js
import myModule from './myModule'

export default {
  ...box,
  ...screen,
  ...basePic,
  ...gam,
  ...countManage,
  ...onboarding,
  ...myModule
}
```

After flattening, components call `proxy.$API.queryMyData(data)`. The current API layer does not use a
`proxy.$API.myModule.queryMyData(data)` namespace.

Request functions use the Axios instance in `utils/request.js`, which attaches the `mtk`, `token`, `fileMode`, `lang`, and `Accept-Language` headers. The default timeout is 20 seconds; configured long-running endpoints use 10 minutes. Business and HTTP errors are rejected and displayed according to the existing error rules. `silentError` suppresses ordinary error messages; only `suppressAuthRedirect: true` suppresses the authentication-expired message and redirect. URL-returning export helpers remain available for direct download requests.

## Internationalization (i18n)

### Setup

- **Default locale**: `zh-CN`
- **Fallback locale**: `en-US`
- Locale preference is persisted in `localStorage` under the `cosmo.locale` key

Translations are managed across three tiers:

| Tier               | Source                                          | Purpose                               |
| ------------------ | --------------------------------------------- | ---------------------------------- |
| Static locale files   | `i18n/locales/{zh-CN,en-US}.js`                  | All built-in UI strings, navigation, validation, status, etc. |
| Short-form glossary      | `i18n/glossary.js`                              | Compact labels for constrained layouts               |
| Dynamic resource i18n      | `public/resource-i18n/resource.{locale}.json`    | Backend config items such as algorithm names, parameters, options |

### Translation API

- **`$t('key.path')`** — Always returns the full translation in the current locale.
- **`$tShort('key.path', scope)`** — Returns the short form only when the glossary permits it for the given scope; otherwise falls back to the full translation.

### Short Scopes

Nine UI context scopes control where compact labels are allowed:

| Scope ID         | Typical Component                    |
| ---------------- | ------------------------------------ |
| `btn.compact`    | Compact buttons (≤100px)             |
| `table.header`   | Table column headers                 |
| `sidebar.menu`   | Sidebar menu (~180px wide)           |
| `flow.node`      | Pipeline orchestration node labels   |
| `dashboard.card` | Dashboard KPI card titles            |
| `tag.badge`      | Status badges                        |
| `inline.action`  | Table row-level action links         |
| `tab.compact`    | Compact tab titles                   |
| `placeholder`    | Input placeholder text               |

## Build Configuration

### Vite Config (`vite.config.js`)

- **Alias**: `@` → `src/`
- **Dev proxy**: `/gtw`, `/event`, `/weblogo`, `/web` → `VITE_APP_API_URL`
- **CSS**: SCSS compiled via `sass-embedded` using the `modern-compiler` API
- **Build chunks** (manual split):
  - `vendor-vue` — Vue + Router + I18n
  - `vendor-element` — Element Plus
  - `vendor-echarts` — ECharts
  - `vendor-vue-flow` — @vue-flow/*
  - `vendor-graph` — @antv/x6, @antv/layout (dependencies currently not installed, reserved)
  - `vendor-lodash` — lodash, dagre
  - `vendor-moment` — moment
  - `vendor-md` — highlight.js, markdown-it (dependencies currently not installed, reserved)
  - `vendor` — remaining dependencies

### Environment Variables

The app reads `VITE_APP_BASE_URL` at build time and uses `VITE_APP_API_URL` for the dev proxy. `.env` files are not tracked in the repository; create one locally as needed.

## State Management

No Pinia or Vuex stores are used. State is managed through:

- **localStorage**: Authentication values (`mtk` and `token`), account info, locale preference, and run mode.
- **Component-local state**: Views manage UI state, including operation loading state, through `data()`, `reactive()`, or `ref()`.
- **Authentication redirects**: The router checks the stored token before entering protected routes. The request utility clears `mtk` and `token` on authentication expiry and navigates to `#/boxLogin`, unless the request explicitly suppresses that redirect.

## Key Utilities

| File                          | Purpose                                                          |
| ----------------------------- | ------------------------------------------------------------- |
| `utils/request.js`            | Axios wrapper; attaches request headers and handles timeouts, error messages, and authentication-expiry redirects |
| `utils/message.js`            | Singleton-deduped `ElMessage` wrapper                        |
| `utils/imagePreview.js`       | Full-screen image preview                                    |
| `utils/resourceLocaleLoader.js` | Fetches dynamic i18n JSON from the server at app start and merges it into vue-i18n |
| `utils/i18nResource.js`       | Resolves the `*I18nKey` fields in backend algorithm/parameter config |
| `utils/whepPlayer.js`         | WHEP WebRTC player based on the native `RTCPeerConnection`   |
