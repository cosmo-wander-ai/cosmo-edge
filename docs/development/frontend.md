---
title: 前端工程
description: 当前 Vue 3 + Vite 前端结构、依赖和构建入口。
prev:
  text: 模型与资源
  link: /reference/models
next:
  text: 后端开发
  link: /development/backend
---

# 前端工程

前端项目位于：

```text
src/web
```

## 技术栈

来自 `src/web/package.json`：

| 类型 | 技术 |
| --- | --- |
| 框架 | Vue 3 |
| 构建 | Vite 6 |
| 路由 | Vue Router 4 |
| UI | Element Plus |
| HTTP | Axios |
| 图表 | ECharts |
| 国际化 | Vue I18n |
| 流程图 | Vue Flow |

## 常用脚本

```bash
cd src/web
npm run build
```

i18n 检查：

```bash
npm run i18n:check
```

`prebuild` 会自动执行 i18n 检查。

## API 目录

```text
src/web/src/api
```

当前可见模块：

- `basePic.js`
- `box.js`
- `countManage.js`
- `gam.js`
- `index.js`
- `login.js`
- `screen.js`

## 菜单入口

菜单配置位于：

```text
src/web/src/views/main/menu.js
```

当前菜单覆盖首页、实时展示、图片分析、视频接入、场景任务、事件中心、模型仓库、底库、数据对接、音频、联动和系统管理等区域。
