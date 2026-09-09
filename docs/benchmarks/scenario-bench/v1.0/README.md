# ScenarioBench v1.0 Results

This directory contains the sanitized ScenarioBench benchmark data referenced by the CosmoEdge v1.0 README and release notes.

## Summary

| ID | Scenario | Hardware profile | Target FPS | Max stable channels | Result | Chinese report |
| --- | --- | --- | ---: | ---: | --- | --- |
| [vlm-55009-npu](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.html) | VLM Review NPU Benchmark (55009) | npu-yy-16t01-preview | 0.1 | 8 | PASS | [zh-CN](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.zh-CN.html) |
| [helmet-7463-npu](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.html) | No Safety Helmet NPU Benchmark (7463) | npu-yy-16t01-preview | 3 | 16 | PASS | [zh-CN](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.zh-CN.html) |
| [pedestrian-45626-npu](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.html) | Pedestrian Detection NPU Benchmark (45626) | npu-yy-16t01-preview | 5 | 16 | PASS | [zh-CN](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.zh-CN.html) |
| [pedestrian-helmet-mixed-npu](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.html) | Pedestrian + No Safety Helmet Mixed NPU Benchmark (45626 + 7463) | npu-yy-16t01-preview | 3 | 16 | PASS | [zh-CN](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.zh-CN.html) |
| [helmet-7463-x86](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.html) | No Safety Helmet x86 Baseline Benchmark (7463) | x86-cpu-baseline | 3 | 7 | LIMITED | [zh-CN](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.zh-CN.html) |

## Files

- `manifest.json` provides a bilingual index for README tables and release automation.
- `environment.md` describes the anonymized hardware profiles, model inputs, and publication policy.
- Each scenario directory contains `summary.json`, English `report.html`, and original Chinese `report.zh-CN.html`.

Raw `metrics.json` files are intentionally kept out of the repository. Publish them with the v1.0 release assets when full sampling traces are required.

For the current formally gated three-platform VLM validation, see the [v1.1 benchmark](../v1.1/README.md). The former single-device page remains a superseded historical observation only.
