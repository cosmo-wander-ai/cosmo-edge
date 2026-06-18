---
title: 模型与资源
description: 当前资源目录、模型模板、算法模板和公开发布注意事项。
prev:
  text: API 概览
  link: /reference/api
next:
  text: 前端工程
  link: /development/frontend
---

# 模型与资源

本文记录当前仓库可确认的模型和资源组织方式。

## 资源目录

| 目录 | 用途 |
| --- | --- |
| `data/resource/aiboxresource` | Sophon 发布包资源 |
| `data/resource/aiboxresource_x86` | x86 Docker/CPU 后端资源 |

构建时通过 `RESOURCE_DIR` 选择资源目录。

## 模型模板

模型模板位于：

```text
data/resource/*/model_template
```

当前可见模板包括：

- YOLO 检测类模板
- YOLO 分类类模板
- DINO
- SAM2
- feature
- keypoints
- Qwen3 / Qwen3VL

## 布局和组件

资源布局文件包括：

```text
data/resource/*/layout/modelComponents.json
data/resource/*/layout/actions.json
data/resource/*/layout/linkageStorages.json
```

这些文件会影响前端配置项、模型组件参数、动作节点和联动策略。

## 算法模板

算法模板位于：

```text
data/resource/*/algorithm_template
```

其中可见视觉语言大模型、DINO、YOLO 等相关模板。正式发布时，建议从当前资源目录生成一份“模板目录表”，避免手写列表过时。

## x86 与 Sophon 差异

x86 路径：

```text
data/resource/aiboxresource_x86
```

Sophon 路径：

```text
data/resource/aiboxresource
```

代码中可见 x86 ONNX 文件和 Sophon model package 的处理差异。完整模型移植流程应结合当前可发布模型包重新验证。

## 开源发布注意事项

正式开源前需要确认：

- 哪些资源文件允许公开发布。
- 是否包含模型权重。
- 模型示例的许可证。
- `prebuild/libcosmo_model_guard.so` 是否可公开。
- 加密模型支持是否属于公开功能。
- 第三方模型生态引用是否需要额外声明。
