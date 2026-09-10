# Runtime resource sets and license boundaries

CosmoEdge selects one explicitly named resource set for each target build. BM1688 and CV186X use
separate top-level directories so package contents and GitHub paths identify the intended platform.

| Target | Resource set | Open benchmark models |
| --- | --- | --- |
| BM1688 | `aiboxresource_bm1688` | `YOLOV8n V1.0.0` detector and `helmet V1.0.0` classifier |
| CV186X | `aiboxresource_cv186x` | Device-verified copies of the same benchmark detector and classifier |
| RK3576 | `aiboxresource_rknn` | Platform-specific RKNN resources |
| RV1126B | Shared `aiboxresource_rknn` templates plus a manifested target artifact bundle |
| x86 | `aiboxresource_x86` | ONNX Runtime resources |

The public ScenarioBench v1.1 Sophon models are present in both platform-scoped resource sets:

| Model | Size | SHA-256 | Repository path |
| --- | ---: | --- | --- |
| YOLOV8n detector | 7,023,600 B | `56b207ef2876da76505e403a049d3c44a411b9fe707ab73dc64f1cd9d9b6c5c8` | `aiboxresource_<platform>/models/prod_BM1688_6047042_YOLOV8n_V1.0.0/` |
| Helmet classifier | 6,001,416 B | `33b0fb4bcb29e41a92f9c1c518671aefc69cbf9207934deaba32ca7cd8cd7c8a` | `aiboxresource_<platform>/models/prod_BM1688_7486163_helmet_V1.0.0/` |

The CV186X copies were retrieved from the benchmark device and match the recorded BM1688 files
byte for byte. The inner `prod_BM1688_` name and `chip_type` value are retained because they are
part of the exact device-loaded package. This measured exception applies only to these two hashes;
other BM1688 and CV186X artifacts remain target-specific.

The resource tree also contains selected public example weights and prebuilt artifacts. The
repository's Apache-2.0 source-code license does not replace a model's own license:

- `aiboxresource_rknn/model-artifacts/` contains explicitly manifested, chip-specific community
  examples. Its Ultralytics YOLOv8 examples are AGPL-3.0 and are not commercial model deliverables.
- Generated overlays stay under `output/platform-artifacts/` and are verified against their source
  bundle before packaging.
- Commercial or proprietary models use a separate ignored artifact manifest with independently
  reviewed provenance and license. They are not derived from, or represented by, the community
  example bundle.

Each bundle's `artifact-manifest.json` and the packaged `resource/model-bundle.json` freeze the
model, source, hash, chip, usage, and license identity.

## 资源目录与许可证说明

BM1688 与 CV186X 分别使用 `aiboxresource_bm1688` 和 `aiboxresource_cv186x`。CV186X
目录中的 `YOLOV8n V1.0.0` 与 `helmet V1.0.0` 来自压测设备，文件大小和 SHA-256 已核对。
其内部 `prod_BM1688_` 名称与 `chip_type` 保留设备原始内容；这只是上述两个固定哈希的实测
例外，不代表其他 BM1688 与 CV186X 模型可互换。

资源目录也包含少量公开示例权重和预编译产物；仓库的 Apache-2.0 源码许可证不会覆盖或
替代模型自身的许可证：

- `aiboxresource_rknn/model-artifacts/` 归档带清单的芯片专属社区示例，其中
  Ultralytics YOLOv8 示例按 AGPL-3.0 分发，不属于商业模型交付。
- 目标 overlay 生成在 `output/platform-artifacts/`，打包前必须与源 bundle 的芯片、
  模型和哈希完全一致。
- 商业或专有模型通过独立、忽略的 artifact manifest 输入，并使用单独审计的来源和
  许可证；它们不派生于、也不冒充社区示例 bundle。

每个 bundle 的 `artifact-manifest.json` 以及包内 `resource/model-bundle.json`
记录模型、源文件、哈希、芯片、用途和许可证身份。
