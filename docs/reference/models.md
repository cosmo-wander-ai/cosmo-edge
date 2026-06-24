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

## 资源授权说明

资源目录中的模型模板、算法模板和布局配置文件描述了模型和算法的元数据，不包含模型权重。部分资源文件为示例用途：

- `prebuild/` 目录中的预编译组件需要单独确认分发许可。
- 模型加密功能是否包含在当前构建中，取决于 CMake 选项 `COSMO_MODEL_GUARD`。
- 如果从第三方模型生态引入模型，请遵循对应模型的许可证要求。
