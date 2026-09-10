# Rockchip community example model artifacts

This directory archives chip-specific RKNN binaries used by the public
CosmoEdge examples and CI. These artifacts are optional community examples;
they are not Cosmo commercial model deliverables and are not the source of
proprietary, independently developed model bundles.

Each target directory contains an `artifact-manifest.json` that freezes the
source model, model specification, converted binary, size, SHA-256, intended
usage scope, and applicable license. Platform resource overlays remain
generated under `output/platform-artifacts/`; shared algorithm and model
configuration templates are not copied per chip.

The bundled Ultralytics YOLOv8 example artifacts are distributed under
AGPL-3.0. Their license text is stored in `LICENSES/AGPL-3.0.txt`. A package
that includes these examples must preserve `resource/model-bundle.json` and
`resource/licenses/model-assets/AGPL-3.0.txt`. Commercial or proprietary model
bundles must provide their own independently reviewed manifest and must not be
represented as this community example bundle.

## 中文说明

本目录只归档公开示例和 CI 使用的芯片专属 RKNN 模型，不属于 Cosmo
商业模型交付，也不代表独立自研模型使用了这些权重、训练代码或架构。

每个平台的 `artifact-manifest.json` 固定源模型、模型规格、转换产物、大小、
SHA-256、用途和许可证。算法及模型配置继续复用共享模板，目标 overlay 仍在
`output/platform-artifacts/` 中生成，不为每颗芯片复制一套资源代码。

其中 Ultralytics YOLOv8 示例按 AGPL-3.0 分发。包含这些示例的包必须保留模型
bundle 清单和许可证；商业或专有模型包必须使用独立审计的清单，不能冒充本示例包。
