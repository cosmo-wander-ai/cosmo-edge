---
title: 前端工程
description: 前端技术栈、项目结构、常用脚本、API 层、i18n、构建配置及新增页面指南。
prev:
  text: 模型与资源
  link: /reference/models
next:
  text: 后端开发
  link: /development/backend
---

# 前端工程

前端项目位于 `src/web/`，是一个 Vue 3 + Vite 单页应用，提供模型管理、场景配置、流水线编排、实时预览、告警视图和系统设置等功能。

## 项目结构

```
src/web/
├── package.json              # 依赖和脚本
├── vite.config.js            # 构建配置、别名、代理
├── scripts/                  # i18n 校验和同步脚本
└── src/
    ├── main.js               # 应用入口
    ├── App.vue               # 根组件
    ├── api/                  # API 模块（7 个文件）
    │   └── index.js          # 合并所有模块，注入全局 $API
    ├── assets/               # 图片、图标、音频资源
    ├── components/           # 共享组件
    ├── i18n/                 # vue-i18n 配置、locale 文件、short-scopes
    ├── micro/
    │   └── state.js          # 轻量全局状态
    ├── router/
    │   └── index.js          # Hash 模式路由（约 25 条）
    ├── styles/
    │   └── global.scss       # 全局 SCSS
    ├── utils/                # Axios 封装、消息提示、图片预览、i18n 加载、WebRTC 播放器
    └── views/                # 页面组件
        ├── main/             # MainLayout（侧栏 + 顶栏 + 菜单）
        ├── home/             # 首页仪表盘
        ├── box/              # 边缘设备视图（摄像头、事件、底库、系统、数据对接）
        └── gam/              # AI 管理视图（任务、模型、编排、图片分析）
```

## 技术栈

| 类别       | 库                   | 版本     | 用途                              |
| ---------- | -------------------- | :------: | --------------------------------- |
| 框架       | Vue 3                | ^3.5.35  | Composition API + `<script setup>` |
| 构建       | Vite                 | 6.3.5    | 开发服务器、HMR、分块生产构建     |
| 路由       | Vue Router           | ^4.2.0   | 基于 Hash 的客户端路由            |
| UI         | Element Plus         | 2.13.2   | 组件库（锁定版本）                |
| 穿梭框     | tree-transfer-vue3   | ^1.2.2   | 算法选择的树形穿梭框组件          |
| HTTP       | Axios                | ^1.7.0   | API 客户端，带拦截器              |
| 图表       | ECharts              | ^6.0.0   | 仪表盘和统计图表                  |
| 流程编辑器 | @vue-flow/core       | ^1.48.2  | 流水线编排编辑器                  |
|            | @vue-flow/background  | ^1.3.2  | 编辑器背景网格                    |
|            | @vue-flow/controls   | ^1.1.3   | 编辑器缩放控件                    |
|            | @vue-flow/minimap    | ^1.5.4   | 编辑器缩略图                      |
| 国际化     | Vue I18n             | ^9.14.5  | 多语言支持                        |
| 视频播放   | flv.js               | ^1.6.2   | FLV 流播放                        |
| WebRTC     | 原生 RTCPeerConnection | —      | WHEP 协议播放                      |
| 布局算法   | dagre                | ^0.8.5   | 流程图布局计算                    |
| 工具库     | lodash, moment, uuid, js-md5, mitt | — | 各类工具函数                  |

## 快速开始

### 环境变量

在 `src/web/` 下创建 `.env` 或 `.env.development` 文件：

| 变量                 | 用途                        |
| -------------------- | --------------------------- |
| `VITE_APP_BASE_URL`  | 应用基础路径（默认 `/`）    |
| `VITE_APP_API_URL`   | 开发代理目标的后端 API 地址 |

### 开发服务器

```bash
cd src/web
npm install
npm run dev
```

开发服务器启动在 `http://localhost:3000`。所有 `/gtw`、`/event`、`/weblogo`、`/web` 请求会代理到 `VITE_APP_API_URL`。

### 生产构建

```bash
cd src/web
npm run build
```

`prebuild` 会在构建前自动执行 `npm run i18n:check`。i18n 校验不通过会阻止构建。

## 常用脚本

| 脚本                        | 用途                                                    |
| --------------------------- | ------------------------------------------------------- |
| `npm run dev`               | 启动 Vite 开发服务器                                    |
| `npm run build`             | 生产构建（通过 `prebuild` → i18n 校验）                 |
| `npm run preview`           | 本地预览生产构建结果                                    |
| `npm run i18n:check`        | 运行全部 5 个 i18n 校验脚本                             |
| `npm run resource-i18n:check` | 检查资源 i18n 同步状态                               |
| `npm run resource-i18n:sync`  | 从后端同步资源 i18n key（提交前检查 diff）           |

i18n 校验覆盖：短词 scope 正确性、locale key 一致性、glossary 同步、弹窗操作按钮标签、未使用 key 检测。

## 新增页面

### 第一步：创建视图组件

在 `src/web/views/` 下的对应子目录中创建新的 `.vue` 文件（设备相关放 `box/`，AI 管理相关放 `gam/`）。

### 第二步：注册路由

在 `src/web/src/router/index.js` 中添加路由：

```js
{
  path: '/myModule/myPage',
  name: 'MyPage',
  component: () => import('@/views/myModule/myPage/index.vue')
}
```

需要侧栏 + 顶栏布局的页面应使用 `MainLayout` 父路由。独立页面（如登录页、大屏）不需要布局包裹。

需要登录的路由会由全局导航守卫校验 `localStorage.getItem('token')`。

### 第三步：添加菜单项

编辑 `src/web/src/views/main/menu.js`，选择对应的 `section`（`"core"`、`"display"`、`"task"`、`"resource"`、`"system"`），添加菜单项：

```js
{
  index: '/myModule/myPage',
  titleKey: 'nav.myPage',
  icon: 'el-icon-xxx',
  section: 'task'
}
```

### 第四步：添加翻译

在以下两个文件中添加新的 i18n key：
- `src/web/src/i18n/locales/zh-CN.js`
- `src/web/src/i18n/locales/en-US.js`

菜单标签放在 `nav` 下，页面专属文案按模块新建顶层 key。

## API 层

### 模式

API 模块位于 `src/web/src/api/`。每个模块导出封装了 `axios` 调用的函数。`api/index.js` 通过对象展开将所有模块合并为一个对象，`main.js` 将其注入为全局 `$API`。

组件中使用方式：`this.$API.dologin(params)` 或 `proxy.$API.dologin(params)`（Composition API）。

### 现有模块

| 模块       | 文件             | 领域                              |
| ---------- | ---------------- | --------------------------------- |
| 认证       | `login.js`       | 登录/登出、验证码、密码重置、用户信息 |
| 设备管理   | `box.js`         | 摄像头、事件、系统设置、音频、联动 |
| AI 管理    | `gam.js`         | 算法、任务、模型、编排、图片分析 |
| 算法管理   | `countManage.js` | 算法增删改查、许可证、硬件信息     |
| 底库       | `basePic.js`     | 人脸库、人体库、物品库、文件导入   |
| 实时流     | `screen.js`      | 摄像头列表、拉流生命周期、WebSocket |

### 新增端点

在对应模块文件中添加函数（或新建文件），通过 `api/index.js` 导出：

```js
// api/myModule.js
export const queryMyData = (params) => request.post('/gtw/cwai/MyModule/Query', params);

// api/index.js
export * as myModule from './myModule.js';
```

所有 API 调用共用 `utils/request.js` 中的 Axios 实例，会自动附加 `mtk`、`token`、`fileMode`、`lang` 等请求头，并处理认证失败的跳转。

## 国际化（i18n）

### 架构

- **默认语言**：`zh-CN`
- **回退语言**：`en-US`
- 语言偏好持久化在 `localStorage` 的 `cosmo.locale` 字段

翻译分三层管理：

| 层级               | 来源                                          | 用途                               |
| ------------------ | --------------------------------------------- | ---------------------------------- |
| 静态 locale 文件   | `i18n/locales/{zh-CN,en-US}.js`（各约 1420 行） | 所有内置 UI 文案、导航、校验、状态等 |
| 短词 glossary      | `i18n/glossary.js`（76 条）                     | 紧凑布局场景下的缩写               |
| 动态资源 i18n      | `public/resource-i18n/resource.{locale}.json`    | 算法名称、参数、选项等后端配置项   |

### 翻译 API

- **`$t('key.path')`** — 始终返回当前语言版本的完整文案。
- **`$tShort('key.path', scope)`** — 仅当 glossary 中该 key 允许在指定 scope 下使用短形式时才返回短词，否则回退到完整文案。

### 短词 Scope

9 种 UI 上下文 scope 控制短词的使用场景：

| Scope ID         | 典型组件                  |
| ---------------- | ------------------------- |
| `btn.compact`    | 紧凑按钮（≤100px）       |
| `table.header`   | 表格列头                  |
| `sidebar.menu`   | 侧栏菜单（约 180px 宽）  |
| `flow.node`      | 流水线编排节点标签        |
| `dashboard.card` | 大屏指标卡标题            |
| `tag.badge`      | 状态徽标                  |
| `inline.action`  | 表格行内操作链接          |
| `tab.compact`    | 紧凑 tab 标题             |
| `placeholder`    | 输入框 placeholder        |

## 构建配置

### Vite 配置（`vite.config.js`）

- **别名**：`@` → `src/`
- **开发代理**：`/gtw`、`/event`、`/weblogo`、`/web` → `VITE_APP_API_URL`
- **CSS**：通过 `sass-embedded` 使用 `modern-compiler` API 编译 SCSS
- **构建分块**（手动拆分）：
  - `vendor-vue` — Vue + Router + I18n
  - `vendor-element` — Element Plus
  - `vendor-echarts` — ECharts
  - `vendor-vue-flow` — @vue-flow/*
  - `vendor-graph` — @antv/x6、@antv/layout（依赖当前未在 package.json 中安装，为预留规则）
  - `vendor-lodash` — lodash, dagre
  - `vendor-moment` — moment
  - `vendor-md` — highlight.js、markdown-it（依赖当前未安装，为预留规则）
  - `vendor` — 其余依赖

### 环境变量

应用在构建时读取 `VITE_APP_BASE_URL`，开发代理使用 `VITE_APP_API_URL`。仓库不跟踪 `.env` 文件，需本地自行创建。

## 状态管理

未使用 Pinia 或 Vuex。状态通过以下方式管理：

- **localStorage**：认证 token（`mtk`）、账户信息、语言偏好、运行模式。
- **`micro/state.js`**：轻量全局响应式对象，管理 loading 和登录状态，供 Axios 拦截器控制加载遮罩。
- **组件内状态**：大部分 UI 状态在各视图组件内通过 `reactive()` / `ref()` 管理。

## 关键工具

| 文件                            | 用途                                                          |
| ------------------------------- | ------------------------------------------------------------- |
| `utils/request.js`              | Axios 实例，自动附加 `mtk`/`token`/`lang` 等头，处理认证失效跳转，上传/升级等长时间请求不显示 loading |
| `utils/message.js`              | 单例去重的 `ElMessage` 封装                                    |
| `utils/imagePreview.js`         | 全屏图片预览                                                  |
| `utils/resourceLocaleLoader.js` | 应用启动时从服务端拉取动态 i18n JSON 并合并到 vue-i18n        |
| `utils/i18nResource.js`         | 解析后端算法/参数配置中的 `*I18nKey` 字段                      |
| `utils/whepPlayer.js`           | 基于原生 `RTCPeerConnection` 的 WHEP WebRTC 播放器            |
