# ScenarioBench v1.0 基准测试结果

本目录保存 CosmoEdge v1.0 README 和发布说明引用的脱敏后 ScenarioBench 基准测试数据。

## 汇总

| ID | 场景 | 硬件档位 | 目标 FPS | 最大稳定路数 | 结果 | 英文报告 |
| --- | --- | --- | ---: | ---: | --- | --- |
| [vlm-55009-npu](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.zh-CN.html) | VLM 算法压测 (55009) | npu-yy-16t01-preview | 0.1 | 8 | 通过 | [English](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.html) |
| [helmet-7463-npu](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.zh-CN.html) | 安全帽算法单跑压测 (7463) | npu-yy-16t01-preview | 3 | 16 | 通过 | [English](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.html) |
| [pedestrian-45626-npu](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.zh-CN.html) | 行人检测单跑压测 (45626) | npu-yy-16t01-preview | 5 | 16 | 通过 | [English](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.html) |
| [pedestrian-helmet-mixed-npu](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.zh-CN.html) | 行人 + 安全帽双算法压测 (45626 + 7463) | npu-yy-16t01-preview | 3 | 16 | 通过 | [English](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.html) |
| [helmet-7463-x86](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.zh-CN.html) | 安全帽算法 x86 基线压测 (7463) | x86-cpu-baseline | 3 | 7 | 受限 | [English](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.html) |

## 文件说明

- `manifest.json` 是中英文共用索引，可用于 README 表格和发布自动化。
- `environment.md` 描述脱敏后的硬件档位、模型输入和发布策略。
- 每个场景目录包含 `summary.json`、英文 `report.html` 和中文原始 `report.zh-CN.html`。

完整 `metrics.json` 不提交到仓库，继续随 v1.0 release asset 发布。

当前三平台 VLM 正式门禁验证见 [v1.1 benchmark](../v1.1/README.zh-CN.md)；此前的单设备页面只保留为已被取代的历史观测。
