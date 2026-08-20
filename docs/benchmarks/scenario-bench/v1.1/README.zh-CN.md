# CosmoEdge 1.1 多平台多路视频分析性能报告

> BM1688、CV186X、RK3576 与 RV1126B 的人员检测、未佩戴安全帽分析和并发混合任务结果。

入口：[中文主报告](report.zh-CN.html) · [English report](report.html) · [测试方法](methodology.md) · [canonical 用例 Schema](results/cases.schema.json)

本页链接的 HTML 报告和聚合索引均在文档构建时生成。仓库只保留 canonical 测量数据，不重复提交报告载荷。

## 并发混合任务矩阵

每路同时运行两个业务任务、三个模型阶段：人员检测包含一个检测阶段；未佩戴安全帽分析包含检测与分类两个阶段。两个业务任务均设为 5 FPS。

| 平台 | 每路任务组成 | 模型阶段/路 | 目标 FPS/任务 | 通过路数 | 业务任务绑定 |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥16 | 32/32 |
| CV186X | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RK3576 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RV1126B | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥4 | 8/8 |

## 单任务容量矩阵

数字表示最后通过路数；`≥` 表示测试设定的最高路数通过。`*` 表示增加下一路时任务绑定被阻断，已通过路数仍保留。`†` 表示扩容轮在开始测量前被存储条件阻断。

| 平台 | 任务 | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | 人员检测 | ≥8 | ≥16 | ≥16 | ≥16 |
| BM1688 | 未佩戴安全帽分析 | ≥7* | ≥14* | ≥16 | ≥16 |
| CV186X | 人员检测 | ≥8* | ≥15* | ≥16 | ≥16 |
| CV186X | 未佩戴安全帽分析 | 6 | ≥13* | ≥16 | ≥16 |
| RK3576 | 人员检测 | 6 | 12 | ≥16 | ≥8† |
| RK3576 | 未佩戴安全帽分析 | 6 | 10 | 12 | ≥16 |
| RV1126B | 人员检测 | 2 | ≥4 | ≥4 | ≥4 |
| RV1126B | 未佩戴安全帽分析 | 2 | ≥4 | ≥4 | ≥4 |

## 统一测试条件

- CosmoEdge 源码：`89c73a7464a81ef378686447d7c1eeb88b988455`，tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`。
- 视频：固定 1920×1080、H.264、24 FPS 本地循环样本，SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`。
- 路数逐路增加；每级保持 30 秒；约每 3 秒采样；不加载预览客户端。
- FPS 达标率门禁 80%，遥测缺失率 0，平均丢弃率上限 5%。
- 未佩戴安全帽任务是“检测 + 分类”两级管线，两级节点使用相同目标 FPS。

BM1688 与 CV186X 使用字节一致的检测和分类模型；RK3576、RV1126B 使用相同公开输入输出合同的平台专用 RKNN 产物。模型完整哈希见 [models](models/model-card.md)。

## Canonical 数据与构建生成报告

49 个小模型用例只在 4 份平台级 canonical JSON 中保存一次。每条记录保留完整实测阶梯，以及冻结分支中原始 `summary.json` 的 SHA-256。双语用例页、平台/工作负载汇总、索引与矩阵都从这些文件生成，不再作为额外证据副本提交。

| 平台 | Canonical 用例 | 构建生成概览 | 构建生成用例页 | 构建生成工作负载报告 | 既有 VLM |
| --- | --- | --- | --- | --- | --- |
| BM1688 | [JSON](results/bm1688/cases.json) | <a href="./results/bm1688/report.zh-CN.html">打开</a> | <a href="./results/bm1688/cases/report.zh-CN.html">打开</a> | <a href="./results/bm1688/single-workload/report.zh-CN.html">单任务</a> · <a href="./results/bm1688/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="./results/bm1688/vlm-observation/report.zh-CN.html">打开</a> |
| CV186X | [JSON](results/cv186x/cases.json) | <a href="./results/cv186x/report.zh-CN.html">打开</a> | <a href="./results/cv186x/cases/report.zh-CN.html">打开</a> | <a href="./results/cv186x/single-workload/report.zh-CN.html">单任务</a> · <a href="./results/cv186x/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="./results/cv186x/vlm-observation/report.zh-CN.html">打开</a> |
| RK3576 | [JSON](results/rk3576/cases.json) | <a href="./results/rk3576/report.zh-CN.html">打开</a> | <a href="./results/rk3576/cases/report.zh-CN.html">打开</a> | <a href="./results/rk3576/single-workload/report.zh-CN.html">单任务</a> · <a href="./results/rk3576/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="./results/rk3576/vlm-observation/report.zh-CN.html">打开</a> |
| RV1126B | [JSON](results/rv1126b/cases.json) | <a href="./results/rv1126b/report.zh-CN.html">打开</a> | <a href="./results/rv1126b/cases/report.zh-CN.html">打开</a> | <a href="./results/rv1126b/single-workload/report.zh-CN.html">单任务</a> · <a href="./results/rv1126b/concurrent-mixed/report.zh-CN.html">混合任务</a> | — |

本次刷新只更新小模型结果。此前的 VLM 观测合并到[一份 canonical 文件](results/vlm-observations.json)中；由于 FPS 未启用门禁，它们仍然只能作为实验观测。

候选绑定的 VLM 压测与长稳验证正在执行，当前保留的观测数据和 30 秒阶梯均不代表这些最终结论。只有在运行完成，并冻结源码、安装包、模型、环境、阈值、持续时间和清理状态后，本文才会更新对应结果。

精简前的完整证据树（含逐用例命令、脱敏日志、summary、metrics 与 HTML）作为独立归档记录在 manifest 中。其哈希已经冻结，但归档状态是**已准备、未发布**，也不进入 Git 仓库。

## 复现文件

- [release-manifest.json](release-manifest.json)：源码、工具、视频和平台身份。
- [methodology.md](methodology.md)：测试步骤与结果判定。
- [scenarios](scenarios/README.md)：脱敏后的公开场景描述。
- <a href="./SHA256SUMS">SHA256SUMS</a>：canonical 仓库源文件哈希；构建生成的公开输出会另行生成完整校验清单。
