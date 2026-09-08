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
| `data/resource/aiboxresource_bm1688` | Sophon BM1688 发布包资源 |
| `data/resource/aiboxresource_cv186x` | Sophon CV186X 发布包资源 |
| `data/resource/aiboxresource_x86` | x86 Docker/CPU 后端资源 |
| `data/resource/aiboxresource_rknn` | Rockchip 目标资源与模型覆盖，结合基础模板使用 |

构建时通过 `RESOURCE_DIR` 选择基础资源目录。

RKNN 构建复用 `aiboxresource_x86` 的基础模板，再叠加目标资源。RK3576 使用 `aiboxresource_rknn`；RV1126B 依据平台 profile 与 artifact manifest 在 `output/platform-artifacts/rv1126b/resource-overlay` 生成覆盖资源。基础模板、目标 overlay 与模型目录共同决定随包内容，实际资源以对应安装包清单为准。

## 模型模板

模型模板位于：

```text
data/resource/*/model_template
```

当前仓库可见以下模型模板。模板定义配置和解析入口，不表示对应权重已随包提供，也不表示所有平台都支持该模型。实际可用性取决于后端、芯片、运行时和已安装模型：

- 检测：`yolov5_det`、`yolov8_det`、`yolov9_det`、`yolov11_det`、`yolov12_det`、`yolo26_det`
- 分类：`classify`（基于 YOLOv8 的分类模板）
- 关键点：`keypoints`
- 特征：`feature`
- 分割：`sam2`
- 目标定位：`dino`
- 视觉语言模型：`qwen3vl`、`qwen3_5`
- 文字识别：`ocr`

> 完整清单以各 `data/resource/aiboxresource_*/model_template/` 目录下的实际文件为准。

## 车牌 OCR 模型包

车牌 OCR 仅识别已经由关键点定位的车牌，不包含整图文字检测。模型包由设备资源管理功能安装，公开仓库只提供 `ocr.json` 模板，不提交权重或私有字典。

每个外部 OCR 包必须包含：

- `config.json`：其中 `model_type` 必须为 `"ocr"`；
- 当前平台的模型文件：Sophon 为 `.nn`，x86 为 `.onnx`；
- 配置指定的 `.txt` 字符表文件。

`models[0].params` 必须显式绑定字符表和 CTC 映射：`character_table_file` 只能是模型包根目录中的 `.txt` 文件，`ctc_blank_index` 指定 blank 类别，`ctc_prepend_tokens` 与 `ctc_append_tokens` 分别定义模型首尾的额外类别，`ctc_class_count` 必须等于模型输出的最后一维。字符表与首尾 token 合并后的索引必须逐项匹配 CTC 类别；不匹配会使 OCR 初始化或推理失败，不会静默漏字。通过“新增模型”向导安装时，OCR 模型与字符表必须同时上传，系统会生成这些绑定字段。

旧车牌识别模型 `2000007` 使用 6625 个类别和 6624 行字符表：第 0 项是 blank，系统在配置中追加一个 ASCII 空格作为最后一个类别。解码会丢弃 blank 与相邻重复类别。

PP-OCR 中文识别模型可使用不含 blank 的 6623 行标准字符表；若模型输出为 6625 类，系统会显式配置前置 blank 和末尾 ASCII 空格。

在场景编排中，按“车牌检测/关联 → 车牌四点关键点 → 文字识别 → 事件上报”连接节点。文字识别仅接受严格四点、顺序为左上、左下、右下、右上的车牌四边形；空识别结果不会产生告警。事件上报节点的告警属性请选择“车辆属性”，以在事件中输出 `plateSrc`、`plate` 和矫正后的车牌裁剪图。

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
data/resource/aiboxresource_bm1688
data/resource/aiboxresource_cv186x
```

代码中可见 x86 ONNX 文件和 Sophon model package 的处理差异。完整模型移植流程应结合当前可发布模型包重新验证。

## 资源授权说明

资源目录既包含模型、算法和布局模板，也包含部分公开示例权重与芯片转换产物。仓库的
Apache-2.0 源码许可证不会自动覆盖模型文件；应以 bundle 清单及目录内许可证为准：

- `prebuild/` 目录中的预编译组件需要单独确认分发许可。
- `data/resource/aiboxresource_rknn/model-artifacts/` 中的 Ultralytics YOLOv8
  芯片产物是 AGPL-3.0 社区示例，不属于商业模型交付。
- 商业或专有模型使用独立的来源、权重、训练和许可证记录，不复用社区示例 bundle 的身份。
- 模型加密功能是否包含在当前构建中，取决于 CMake 选项 `COSMO_MODEL_GUARD`。
- 如果从第三方模型生态引入模型，请遵循对应模型的许可证要求。
