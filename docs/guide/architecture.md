---
title: 架构概览
description: 当前 C++ 后端、Vue 前端、服务注册、API、媒体和推理模块概览。
prev:
  text: 部署指南
  link: /guide/deployment
next:
  text: API 概览
  link: /reference/api
---

# 架构概览

CosmoEdge 当前仓库是完整产品运行时，而不只是单个推理库或后端程序。

## 总体结构

```text
+---------------------------------------------------------------+
| Web Console                                                   |
| Vue 3 | Vite | Element Plus | Vue Flow | ECharts | i18n        |
+-------------------------------+-------------------------------+
                                | HTTP / WebSocket
                                v
+---------------------------------------------------------------+
| C++ Backend Runtime                                           |
| API Router | Services | Flow | Media | Inference | Database    |
+-------------------------------+-------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Runtime Package                                               |
| cosmo-engine | nginx | SRS | scripts | resources | data | fonts   |
+---------------------------------------------------------------+
```

## 后端入口

| 入口 | 说明 |
| --- | --- |
| `src/app/main.cc` | 创建 `cosmo::app::Application` |
| `src/app/application.cc` | 应用启动外壳 |
| `src/app/app_init.cc` | 服务注册、初始化、网络服务启动 |
| `src/app/AppConstants.h` | 默认 HTTP / WebSocket 端口 |

主可执行目标：

```text
cosmo-engine
```

## 服务注册

后端通过 `cosmo::service::ServiceRegistry` 进行服务装配。

启动流程分为：

1. 注册基础设施服务。
2. 注册业务服务。
3. 初始化服务。
4. 启动 MQTT、HTTP、WebSocket、设备发现、存储清理、看门狗等运行服务。

## 主要源码目录

| 目录 | 说明 |
| --- | --- |
| `src/api` | API 路由和消息处理 |
| `src/app` | 应用入口和启动流程 |
| `src/db` | DAO 和数据库支持 |
| `src/flow` | 任务、算法、动作链路 |
| `src/infer` | 模型推理封装 |
| `src/linkage` | 告警联动 |
| `src/media` | 视频解码、编码、帧处理、OSD |
| `src/mem` | 内存池和设备内存抽象 |
| `src/network` | HTTP、MQTT、网络消息 |
| `src/nn` | 推理后端抽象 |
| `src/platform` | 平台相关能力 |
| `src/service` | 服务接口和实现 |
| `src/util` | 通用工具 |
| `src/web` | Vue 3 前端 |

## 前端

前端路径：

```text
src/web
```

已确认技术栈：

- Vue 3
- Vite 6
- Vue Router 4
- Element Plus
- Axios
- ECharts
- Vue I18n
- Vue Flow

前端构建产物会被安装到发布包的 `web` 目录。

## 推理和模型

当前项目存在两条推理后端路径：

- x86 CPU 后端，使用 ONNX Runtime。
- Sophon 后端，用于 aarch64/Sophon 发布包。

资源目录：

- `data/resource/aiboxresource`
- `data/resource/aiboxresource_x86`

当前模板覆盖检测（YOLO v5/v8/v9/v11/v12/26）、分类、关键点、特征、分割（SAM2）、目标定位（DINO）以及视觉语言模型（Qwen3VL、Qwen3.5）。完整清单以 `data/resource/aiboxresource/model_template/` 与 `data/resource/aiboxresource_x86/model_template/` 目录下的实际模板文件为准。

