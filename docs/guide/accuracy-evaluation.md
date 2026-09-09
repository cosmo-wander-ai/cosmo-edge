---
title: 视频样本级算法表现测量
---

# 视频样本级算法表现测量

`cosmo-accuracy` 的目标很直接：把一组视频样本送入真实 CosmoEdge 设备，读取持久化告警
事件，并统计算法在这些样本上的表现。

工具回答的是：

- 正检视频是否至少产生预期数量的事件；
- 误检视频是否保持在允许的最大事件数以内；
- 每个算法的正检命中率、误检无告警率和误报率是多少；
- 不同算法、配置、软件版本或并发下，结果和耗时发生了什么变化。

这是事件级、样本级测量。它没有帧级真值或目标框匹配，因此不计算 Precision、Recall、
F1、mAP 或 IoU，也不承担产品门禁、批准基线或发布认证。

## Suite 与样本

真实 suite、视频和结果保存在仓库外的私有目录：

```text
private-suite/
├── suite.yml
├── cases.jsonl
├── task-configs/
│   └── no-helmet.json
└── videos/
```

任务需要设备实际使用的算法 ID、计划 ID，以及以下两种配置来源之一：

- `taskConfig`：引用导出的配置 JSON；
- `configSource: device-default`：运行时读取设备默认配置。

使用本地视频和冻结配置时，配置需要令视频循环播放：

```json
{"key":"param.videoRepeatCount","value":"0"}
```

每个 `cases.jsonl` 行描述一个视频和事件期望：

```json
{"id":"helmet-positive-0001","task":"no-helmet","file":"videos/no-helmet/positive-0001.mp4","sha256":"<sha256>","expectation":{"minEvents":1},"tags":["warehouse","quick"]}
```

误检样本通常使用：

```json
{"expectation":{"maxEvents":0}}
```

case ID 必须唯一，路径必须位于数据根目录内，SHA-256 用于确认运行的确处理了预期文件。
suite 不需要 gates 或重复策略。旧 suite 中已有的 `gates`、`trials` 和 `critical` 可以暂时
保留以兼容原数据；`gates`/`trials` 被忽略，`critical` 只作为旧数据的 quick 选样标记，
不会触发重复测量。

## 从旧目录生成草稿

```bash
cd tools/scenario-bench
node src/accuracy-cli.js init-suite \
  --input-root /private/dataset \
  --output /private/suite-draft \
  --target-chip bm1688
```

命令识别“正检/误检”文件名并生成 case 与文件 hash。它不会创建伪 taskConfig，也不会猜测
算法 ID 或计划 ID；补全这些设备必需值后，将 `suite.draft.yml` 改名为 `suite.yml`。

## 运行前检查

```bash
export COSMO_ACCURACY_TOKEN='<short-lived-token>'
node src/accuracy-cli.js doctor \
  --profile full \
  --concurrency 1 \
  --suite /private/suite/suite.yml \
  --data-root /private/dataset \
  --target-chip bm1688 \
  --device "${COSMO_DEVICE_URL}" \
  --token-env COSMO_ACCURACY_TOKEN
```

也可以使用 `--user <account> --password-stdin`。命令拒绝明文 `--password`。

`doctor` 只检查本次实际选中的视频，包括：

- suite、case/task 引用、相对路径和 SHA-256；
- ffprobe 能否识别视频流和有限时长；
- 设备登录、目标芯片、算法和计划；
- 当前并发所需的上传空间；
- Event/Page 是否可读；
- RTSP 模式需要的 ffmpeg、HTTP 源和 MediaMTX。

设备上已有的 `acc-*` 临时对象会显示为警告，但不阻止新测量，因为每次 trial 使用唯一名称。
真正缺失的视频、算法、计划或设备能力会在任何写操作前停止运行。

## 运行与并发

```bash
node src/accuracy-cli.js run \
  --profile full \
  --concurrency 2 \
  --suite /private/suite/suite.yml \
  --data-root /private/dataset \
  --target-chip bm1688 \
  --device "${COSMO_DEVICE_URL}" \
  --token-env COSMO_ACCURACY_TOKEN \
  --output /private/runs/helmet-v1
```

规则只有几条：

- 每个选中样本取得一个有效 PASS 或 FAIL；
- 基础设施 ERROR 可以按 suite 默认值重试；
- 算法 FAIL 是结果数据，不触发第二轮确认；
- 命中率只统计有效 PASS/FAIL，基础设施 ERROR 单独显示且不进入分母；
- 完整测量即使含 FAIL 也退出 `0`；
- 无法完成 trial 或严格清理失败时退出 `2`；
- CV 可用 `--concurrency 1|2|4` 并行；
- VLM 始终在全部 CV 结束后串行运行。

`--profile full` 运行完整选择集。`--profile quick` 选择带 `quick` tag 的代表样本；还可以用
`--case`、`--task` 或 `--tag` 直接筛选。过滤和并发只是执行选择，不会把结果降级或取消
某种资格，因为工具没有资格/基线概念。

## 单个样本如何测量

每个 trial：

1. 再次校验视频 hash；
2. 上传本地视频或启动唯一 RTSP 流；
3. 创建唯一 `acc-*` 通道并应用任务配置；
4. 等待摄像头在线和 Decode/Detector 处理计数前进；
5. VLM 额外等待任务本地完成计数和直接 Qwen 延时；
6. 在观察窗口内从 Event/Page 查询持久化事件；
7. 达到无上限的 `minEvents` 或超过 `maxEvents` 时可提前停止；
8. 停止任务后分页查询，等待事件 ID 集合收敛并去重；
9. 按最终事件数判定 PASS/FAIL；
10. 保存告警图片，并删除本 trial 的任务和通道；
11. 反查清理结果并写入断点。

WebSocket 不作为计数来源。告警事件必须同时符合通道、算法和时间窗口，避免别的任务污染
结果。每个 trial 的严格清理失败会变成 ERROR；设备上与本 run 无关的旧 `acc-*` 对象不会
覆盖已经完成的测量。

## 输出和退出结果

```text
run-dir/
├── run.private.json
├── run.partial.json
├── summary.json
├── report.html
├── integrity.json
└── artifacts/alerts/
```

- `run.private.json` 保存完整 case、trial、事件、实际配置 hash、耗时和清理结果；
- `summary.json` 保存脱敏后的算法/配置身份、样本状态、micro/macro 指标和 coverage；
- `report.html` 是离线可读报告；
- `integrity.json` 保存已生成文件和图片的 hash；
- `run.partial.json` 支持在相同输入、设备和工具身份下继续未完成 case。

原始私有结果和脱敏 summary 会先写入。summary 校验、HTML 或 integrity 生成失败只产生警告，
不会把已经完成的算法测量改写为失败。摘要不会包含密码、token、设备地址、SN、内部通道
ID、绝对路径或告警 URL。

## 比较测量结果

```bash
node src/accuracy-cli.js compare \
  --reference /private/runs/reference/summary.json \
  --candidate /private/runs/algorithm-b/summary.json \
  --candidate /private/runs/concurrency-2/summary.json \
  --output /private/comparisons/algorithm-and-speed
```

比较要求选中的视频样本一致，这保证分母相同。算法 ID、taskConfig hash、并发、软件版本、
source mode、目标芯片、suite 和工具版本可以不同；这些差异会列在 `contextChanges` 中，而
不会成为拒绝比较的资格条件。

输出包含墙钟耗时、trial 累计工作量、加速比、正检/误检指标百分点变化、每算法变化和全部
case 状态转换。结果写入 `comparison.json`、`report.html` 和 `integrity.json`。

## 阈值诊断与 RTSP

`diagnose-threshold` 接受任意已测得的 FAIL case，并只扫描 suite 为该任务明确列出的参数和值。
每个值运行三个隔离 trial，用于区分稳定通过、稳定失败和波动；它不会自动修改 suite、设备
默认配置或原测量结果。

`local` 直接上传原视频，是默认测量路径。`rtsp-deterministic` 使用 HTTP → ffmpeg →
MediaMTX → 设备 RTSP，用于观察摄取链路影响。两种 source mode 都能参与比较，报告会明确
显示差异。
