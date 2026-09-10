# scenario-bench

`scenario-bench` 是 CosmoEdge 场景任务压测工具。它在控制 PC 上运行，通过设备 HTTP API 完成算法编排导入、视频通道准备、任务绑定、阶梯式并发加压、运行指标采样和报告生成。

工具目标是产出可复现、可解释的容量上限结论，例如“某设备、某软件版本、某算法编排、某输入视频下，容量上限为 15 路；16 路因平均丢弃率超过阈值失败”。

设计背景见 [docs/guide/scenario-benchmark-design.md](../../docs/guide/scenario-benchmark-design.md)。

## 环境要求

- Node.js >= 20
- 控制 PC 可访问被测设备 Web/API 地址
- 已在 CosmoEdge Web 页面完成目标场景任务编排，并导出算法编排模板
- 场景包中包含导出的算法编排模板和输入视频

安装依赖：

```bash
cd tools/scenario-bench
npm install
```

## 快速运行

建议先执行 `doctor`，确认场景包、视频、输出目录和设备登录均正常：

```bash
node src/cli.js doctor \
  --scenario scenarios/no-helmet-99898-fps5-20260630 \
  --output reports/no-helmet-99898-fps5-host22 \
  --device http://192.168.0.22 \
  --user admin \
  --password admin
```

自动化环境推荐通过环境变量传入设备短期 token，避免凭据出现在命令行历史和报告中：

```bash
export COSMO_BENCH_TOKEN='<short-lived-token>'
node src/cli.js doctor \
  --scenario scenarios/no-helmet-99898-fps5-20260630 \
  --device http://192.168.0.22 \
  --token-env COSMO_BENCH_TOKEN
```

检查通过后运行压测：

```bash
node src/cli.js run \
  --device http://192.168.0.22 \
  --user admin \
  --password admin \
  --scenario scenarios/no-helmet-99898-fps5-20260630 \
  --output reports/no-helmet-99898-fps5-host22 \
  --cleanup \
  --verbose
```

输出目录包含：

| 文件 | 说明 |
| --- | --- |
| `report.html` | 面向人工阅读的报告，包含容量结论、判定口径、路数结果和阈值明细 |
| `summary.json` | 机器可读摘要，适合后续做批量汇总或版本对比 |
| `metrics.json` | 完整采样数据，包含每个 tick 的通道指标和硬件资源 |
| `metrics.partial.json` | 运行过程中每 30 秒更新的临时采样文件，用于异常中断兜底且不让证据写盘干扰采样周期 |

长稳任务运行期间可对临时采样做只读检查点审计。检查点不会停止任务，也不会把“尚未跑满时长”误判为通过：

```bash
node src/cli.js checkpoint \
  --input reports/rk3576-12h/metrics.partial.json \
  --identity reports/rk3576-12h/identity.txt \
  --output reports/rk3576-12h/checkpoint-12h.json \
  --gate-hours 12 \
  --max-gap-sec 120 \
  --min-fps-ratio 0.9 \
  --expected-preview-streams 4 \
  --expected-decoder-backend rockchip-mpp-rga \
  --expected-encoder-backend rockchip-mpp-rga \
  --min-rga-bound-native-int8-frames 1 \
  --min-rknn-mpp-dmabuf-frames 1 \
  --min-rknn-rga-crop-dmabuf-frames 1 \
  --max-rga-bound-requantize-avg-ms 2
```

输出结论只有三种：`IN_PROGRESS` 表示时长尚未达到且其余门禁正常（命令退出码 3），`FAIL` 表示连续性、负载、资源或原生媒体链路存在失败（退出码 1），`PASS` 仅在时长达到且全部必需检查通过时产生（命令退出码 0）。`gate-hours` 从首次观测到目标路数、活动路数和唯一通道数均达到配置负载时开始累计；登录、建连和部分路数爬坡只计入 `runAgeSec`，不计入验收使用的 `fullLoadCoverageSec`。审计覆盖采样间断/新鲜度、固定路数、逐通道 FPS 与丢帧、CPU/内存/磁盘、内存池增长、MPP/RGA/RKNN 失败及计数器回退、延迟 Copy-out 记账、OSD/编码/发布帧流和 SRS 发布流；针对原生 RKNN 路径还可要求 MPP DMA-BUF 输入、分类裁剪 DMA-BUF、全部 forward 的 INT8 直绑覆盖和必要符号位变换的平均耗时。输入和候选身份文件的 SHA-256 会写入结果。

## 创建场景包

场景包依赖一个预先导出的算法编排模板。该模板不是工具自动生成的，需要先在 CosmoEdge Web 页面上完成一次场景任务编排，然后从平台导出。

推荐流程：

1. 在被测设备或同软件版本设备上，通过 Web 页面创建目标场景任务。
2. 完成算法流程、原子能力、阈值、FPS、告警节点、调度等业务参数配置。
3. 确认该任务可以在单路视频上正常运行。
4. 从平台导出算法编排模板，保存为 `algorithm-template.json`。
5. 使用 `init-scenario` 将模板、输入视频和压测阶梯配置组装成场景包。

`scenario-bench` 会在压测开始前调用 `algorithm/layout/save`，把这个导出的编排模板导入到本次被测设备上。因此，同一个场景包可以复用于同型号或同软件版本设备的横向对比；但如果算法版本、编排结构或业务参数发生变化，应重新导出模板。

可以用 `init-scenario` 从导出的算法模板和视频快速生成场景包：

```bash
node src/cli.js init-scenario \
  --name my-scenario \
  --template C:/path/to/algorithm-template.json \
  --video C:/path/to/input.mp4 \
  --output scenarios/my-scenario \
  --display-name "未戴安全帽检测 - fps5" \
  --algorithm-id 99898 \
  --schedule-id e89c6c6385e5454b35cde0d1653vg
```

生成后建议立即执行：

```bash
node src/cli.js doctor --scenario scenarios/my-scenario --output reports/my-scenario
```

## 场景包结构

一个场景包是一个目录，`scenario.yml` 是唯一配置入口。单任务和多任务使用同一套模型：单任务只是 `tasks` 里只有一个元素。

| 文件 | 说明 |
| --- | --- |
| `scenario.yml` | 场景名称、通道池、任务列表、容量扫描范围和阈值 |
| `algorithm-template.json` 或其他模板 JSON | 从平台导出的算法编排模板；每个 task 可指定自己的模板 |
| `*.mp4` | local 模式下的本地输入视频 |

`scenario.yml` 示例：

```yaml
name: "no-helmet-99898-fps5"
displayName: "未戴安全帽检测(算法 99898, fps5)"
sampleIntervalSec: 3

channels:
  mode: local
  repeatCount: 0
  sources:
    - name: "no-helmet-01"
      file: "LX0000000007.mp4"

tasks:
  - id: no-helmet
    displayName: 未戴安全帽检测
    type: cv
    algorithmId: "99898"
    scheduleId: "e89c6c6385e5454b35cde0d1653vg"
    template: algorithm-template.json

loadProfile:
  - channels: 1
    holdSec: 30
  - channels: 24
    holdSec: 30

thresholds:
  pass:
    maxCriticalPathLatencyMs: 200
    maxDetectorLatencyMs: 150
    avgDiscardRate: 0.05
    maxPacketDiscardRate: 0.01
```

默认运行模式为 `--profile capacity`，工具会将 `loadProfile` 展开为连续路数扫描。以上配置表示最多扫描到 24 路，实际执行路数为 `1,2,3,...,24`。`holdSec` 使用相邻配置的保持时间；常规场景中保持各项一致即可。若某个 step 省略 `holdSec`，工具会按 workload 类型填默认值：纯 CV 场景 30s，包含 VLM 任务的场景 60s；显式配置始终优先。

核心语义：

- `channels` 定义通道池和视频源；`repeatCount: 0` 表示 local 视频循环播放。
- `tasks` 定义可绑定到通道的算法任务，每个任务有自己的算法模板、调度和任务类型。
- 工具会自动解析模板：存在 `DA_00003`/`PDA_00003` 时识别为可观测的 VLM 任务，
  无需手工修改 `type`。
- `BA_00004.enableLlmReview=1` 属于告警节点内部的嵌套审核。当前 RunningDetail 不暴露
  该内部调用的计数与耗时，因此工具会在运行前明确拒绝这类场景，避免回退到 detector
  指标后产生看似正常但错误的报告。
- `bindings` 可选，定义任务与通道的绑定矩阵；省略时所有任务绑定当前阶梯中的全部通道。
- 单任务、多任务使用同一条执行链路：导入所有任务模板 -> 创建通道池 -> 按阶梯增加通道 -> 按绑定矩阵批量绑定任务 -> 采样 -> 判定 -> 生成报告。

`bindings[].channels` 可以使用：

| 写法 | 含义 |
| --- | --- |
| `all` | 绑定当前阶梯中的全部通道 |
| `[1, 2, 3]` | 绑定第 1/2/3 个通道（1-based） |
| `{ from: 1, to: 8 }` | 绑定第 1 到第 8 个通道 |

报告会同时输出整体容量结论和分任务汇总。整体容量仍表示“该 workload 在当前判定阈值下可稳定支撑的最大通道数”；分任务汇总用于定位多任务压测中是哪一个任务先成为瓶颈。

### 任务策略与 VLM

`tasks[].type` 会选择不同的判定策略：

| type | 策略 | 默认判定口径 |
| --- | --- | --- |
| `cv` | 传统视觉算法 | 检测/关键链路延时、丢弃率；运行期 FPS 熔断使用 1 路基线折半 |
| `vlm` | 视觉语言模型分析 | 分析 FPS 达标率、采样缺失率、可选端到端延时；运行期不使用基线折半 |

模板包含 `DA_00003`/`PDA_00003` 时会自动识别为 VLM；若该节点配置了 `fps`，工具会
自动提取为 `targetFps`。模板没有配置时必须显式给出，例如 0.2fps：

```yaml
tasks:
  - id: vlm
    displayName: 0.8B VLM 分析
    type: vlm
    algorithmId: "12345"
    scheduleId: "e89c6c6385e5454b35cde0d1653vg"
    template: vlm-template.json
    targetFps: 0.2

thresholds:
  taskTypes:
    vlm:
      minFpsRatio: 0.8
      maxMissingRate: 0
      maxEndToEndLatencyMs: 30000
      avgDiscardRate: 0.05
```

`local` 模式下，直接 VLM 任务会自动把 `targetFps` 注入为 `param.videoReadFps`，
让 demux 按分析频率取帧，避免 0.1fps 级别的大模型被源视频原生帧率持续推帧压爆。
VLM 场景省略 `loadProfile[].holdSec` 时默认保持 60s，给模型加载、低频推理完成计数和稳定窗口统计留出足够观察时间；需要压长稳态时仍可显式写成 120s 或更长。

阈值合并顺序为：策略默认值 -> `thresholds.pass` -> `thresholds.taskTypes.<type>` 或 `thresholds.strategies.<type>` -> `thresholds.tasks.<taskId>`。因此可以在同一个 workload 内让 CV 和 VLM 使用不同规则。VLM 不会继承全局 `maxDetectorLatencyMs`、`maxCriticalPathLatencyMs`、`maxAvgNodeLatencyMs`，端到端延时需要在 `taskTypes.vlm` 或 `tasks.<taskId>` 下显式配置。

## 命令说明

### `doctor`

用于运行前检查。默认检查本地环境和场景包；如果提供设备参数，则同时检查设备登录和设备信息。

```bash
node src/cli.js doctor --scenario <dir> [--output <dir>] [--device <url> --user <u> --password <p>]
```

检查项包括：

- Node.js 版本
- `scenario.yml`、算法模板和 local 视频文件
- 算法 ID、目标 FPS、视频模式、并发阶梯
- `layout/save` payload 可生成
- local 视频文件存在
- 输出目录可写
- 设备登录和设备信息查询

### `run`

执行完整压测。

```bash
node src/cli.js run --device <url> --user <u> --password <p> --scenario <dir> --output <dir> [options]
```

常用参数：

| 参数 | 说明 |
| --- | --- |
| `--cleanup` | 运行结束后删除本次创建的通道，推荐开启 |
| `--verbose` | 打印每次采样日志，便于排查 |
| `--skip-import` | 跳过 `algorithm/layout/save`，仅在设备上已存在对应编排时使用 |
| `--no-reuse` | 不复用已有 bench 通道，总是新建 |
| `--profile <mode>` | `capacity` 默认，连续扫描路数上限；`configured` 按 `scenario.yml` 原始配置执行 |
| `--ramp-batch-size <n>` | 每次 ramp 新增通道数，默认 1 |
| `--ramp-batch-delay-sec <n>` | ramp 批次间隔，默认 15 秒 |
| `--token-env <name>` | 从指定环境变量读取短期 token；与 `--user/--password` 二选一 |
| `--preview <mode>` | `none`、`raw` 或 `algorithm`；默认 `none` |
| `--preview-streams <n\|all>` | 每阶梯开启预览的通道数；默认全部 |
| `--preview-clients <n>` | 每路预览的并发解码客户端数；默认 1 |
| `--media-base <url>` | HTTP-FLV 服务基址；启用预览时必填 |
| `--srs-api <url>` | SRS API 基址，用于采集发布流和客户端数 |

启用真实预览客户端时，工具会在设备登录、编排导入和通道创建之前执行
`ffmpeg -version` 预检；专项 `preview` 命令还会同时预检 `ffprobe`。缺失或
不可执行时会直接失败，避免测试环境问题在设备产生临时配置后才暴露。

### 预览负载矩阵

正式验收应至少覆盖以下四种负载，它们分别隔离算法基线、单路媒体开销、随路数增长的媒体开销和同流多客户端分发开销：

| 模式 | 参数 | 验证目标 |
| --- | --- | --- |
| A | `--preview none` | 算法容量基线，不引入预览链路 |
| B | `--preview algorithm --preview-streams 1 --preview-clients 1` | 单路算法预览的解码、OSD、编码、发布开销 |
| C | `--preview algorithm --preview-streams all --preview-clients 1` | 每路均预览；建议按 1/4/8/目标上限路配置执行 |
| D | `--preview algorithm --preview-streams 1 --preview-clients 4` | 同一路流的多客户端播放与 SRS 分发能力 |

示例：

```bash
node src/cli.js run \
  --device http://192.168.0.22 \
  --token-env COSMO_BENCH_TOKEN \
  --scenario scenarios/my-scenario \
  --output reports/my-scenario-preview-d \
  --profile configured \
  --preview algorithm \
  --preview-streams 1 \
  --preview-clients 4 \
  --media-base http://192.168.0.22:18088 \
  --srs-api http://192.168.0.22:1985 \
  --cleanup
```

### `preview`

对一个已运行的确定性测试通道执行媒体专项验收。该命令会真实探测并解码 HTTP-FLV，而不是只检查 API 返回值；覆盖 H.264 编码、分辨率、连续帧和时间戳、算法 OSD 像素差异、同流多客户端、SRS 发布/客户端计数、前端断开/后端停止、资源释放、非法通道/算法，以及可选的同流反复启停。

```bash
node src/cli.js preview \
  --device http://192.168.0.22 \
  --token-env COSMO_BENCH_TOKEN \
  --channel LX0000000007 \
  --algorithm 7463 \
  --mode both \
  --media-base http://192.168.0.22:18088 \
  --srs-api http://192.168.0.22:1985 \
  --clients 4 \
  --lifecycle-iterations 1000 \
  --min-overlay-pixel-delta 1 \
  --output reports/preview-validation
```

输出包含 `preview-validation.json`、`preview-validation.md` 和原始/算法代表帧 PPM 截图。命令默认完成清理；验收结束后仍应确认 `activePreviewStreams` 和 `activePreviewPublishers` 均回到基线。

### `init-scenario`

生成新的场景包。

```bash
node src/cli.js init-scenario --name <name> --template <algorithm-template.json> --video <file> [options]
```

常用参数：

| 参数 | 说明 |
| --- | --- |
| `--output <dir>` | 新场景目录，默认 `scenarios/<name>` |
| `--display-name <name>` | 报告展示名称 |
| `--algorithm-id <id>` | 算法 ID；不传时从模板推导 |
| `--schedule-id <id>` | 调度 ID |
| `--target-fps <n>` | 当模板无法暴露目标 FPS 时写入 `tasks[].targetFps` |

`init-scenario` 会识别模板中的直接 VLM 节点（`DA_00003`/`PDA_00003`）：VLM 场景生成 `type: vlm`，默认阶梯保持 60s；其他 CV 场景默认 30s。

## 判定口径

报告面向“路数上限”输出。默认 `capacity` 模式会从 1 路开始逐路增加并保持采样，直到达到配置上限或触发失败/熔断。

| 概念 | 含义 |
| --- | --- |
| 容量上限 | 最后一个完整执行、且所有报告阈值均 PASS 的连续路数 |
| 路数 PASS/FAIL | 按 `scenario.yml` 中 `thresholds` 的阈值对某个路数进行判定 |
| 瓶颈停止 | 运行期保护熔断，用于避免继续加压导致设备不可用 |
| 基线 FPS | 第 1 阶梯稳定窗口的最低处理 FPS；CV 为检测吞吐，直接 VLM 为推理完成吞吐 |

例如某次运行中：

- 1-15 路均 PASS
- 16 路平均丢弃率 `0.0418 > 0.02`，因此 16 路 FAIL
- 工具停止继续加压

此时准确结论是“容量上限 15 路”。失败路数及失败原因仍会保留在报告明细中。

如需只按 `scenario.yml` 中列出的阶梯执行，可显式使用 `--profile configured`。此模式只能给出已验证配置点，不能给出连续容量上限。

## 指标聚合

每个阶梯会持续采样。为了降低通道刚绑定时的瞬态抖动影响，报告汇总只使用该阶梯后半段采样点作为稳定窗口。

主要指标口径：

| 指标 | 聚合方式 |
| --- | --- |
| 处理 FPS | CV 任务取检测吞吐；直接 VLM 任务使用稳定窗口累计完成数差分计算推理完成吞吐，避免低频任务被单个采样点 0 FPS 误判 |
| 检测 FPS | 前置 CV 检测吞吐，仅作诊断参考，不作为 VLM 任务的处理吞吐 |
| VLM 平均延时 | RunningDetail 中 Qwen3VL 节点的 `durationAvgUs`，换算为毫秒 |
| 平均丢弃率 | 先计算每个通道稳定窗口平均丢弃率，再对通道取平均；报告 PASS/FAIL 使用该值 |
| 最差通道丢弃率 | 先计算每个通道稳定窗口平均丢弃率，再取通道间最大值；用于辅助定位单路异常，不作为默认容量判定 |
| 检测节点延时 | 单通道稳定窗口内取平均，再取通道间最大值 |
| 关键链路延时 | CV 包含解码、检测、跟踪、分类、判断；直接 VLM 任务额外加入 Qwen3VL 节点平均耗时 |
| NPU/CPU/内存峰值 | 稳定窗口内硬件资源峰值 |
| 媒体分阶段耗时 | 按累计计数差分计算预处理、推理、后处理、OSD 和 Publish 耗时 |
| 首帧耗时 | 预览发布器从启动到首个已发布帧的平均值与最大值 |
| 预览生命周期 | 活跃预览流/发布器峰值、原始/算法预览峰值、启动/停止/失败计数增量 |
| SRS 分发 | 活跃发布流和播放客户端峰值 |

媒体阶段指标来自 `QueryHardwareResource.resData.accelerator` 的平台中立累计计数。报告按每个稳定窗口的首尾样本做差分，并明确显示无样本值，不会把缺失遥测伪装成 0。启用预览的报告会额外生成媒体阶段表，用于区分算法推理、OSD、发布和 SRS 分发环节。

报告 PASS/FAIL 使用 `scenario.yml` 中的 `thresholds`。运行期瓶颈停止使用独立保护规则，典型规则包括：

- 稳定窗口最低 FPS 低于第 1 阶梯基线的 50%
- 运行期通道平均丢弃率连续采样超过 5%
- CPU、NPU 或内存连续高位运行

VLM 模式还执行有效性校验：缺少直接 VLM 节点遥测时，不会继续使用前置 detector
指标给出 PASS。`BA_00004` 内嵌审核由于平台接口不可观测，ScenarioBench 会直接拒绝
运行并给出原因。

## 输入模式

当前主要支持 `local` 模式：工具将本地视频上传到设备，再创建本地视频通道。该模式适合作为容量基准，因为输入内容固定，跨版本和跨设备对比更稳定。

`rtsp-fidelity` 和 `rtsp-deterministic` 是预留模式，适合后续扩展到生产 RTSP 保真或可控 RTSP 推流。

## 编排导入

默认运行会调用 `algorithm/layout/save` 将 `algorithm-template.json` 导入设备。这个文件应来自 Web 页面中已配置完成的场景任务导出包，包含算法流程、原子能力、节点参数和任务配置等信息。工具不会自动设计或生成算法流程，只负责导入已有编排并对其做并发压测。

工具会对平台导出的模板做必要的 payload 归一化，例如将 `algorithmCategory`、`algorithmUsage` 转为字符串，以适配后端 DTO。

仅当确认设备上已经存在对应算法编排时，才建议使用 `--skip-import`。如果跳过导入但设备上没有对应编排，任务绑定可能失败，采样会缺失有效数据。

## 使用建议

- 每次正式压测前先运行 `doctor`。
- 正式压测建议使用 `--cleanup`，避免通道残留影响后续测试。
- 不同设备、软件版本、算法模板或输入视频应使用不同输出目录。
- 对外引用结果时优先使用 `summary.json` 和 `report.html` 首页结论。
- 如需做版本趋势对比，使用 `summary.json` 汇总，不直接解析 HTML。

## 视频样本级算法表现测量（cosmo-accuracy，protocol v4）

`cosmo-accuracy` 只做一件事：把视频样本送到真实 CosmoEdge 设备，按持久化事件统计
正检命中率、误检无告警率和每个样本的 PASS/FAIL。它不是帧级标注工具，不计算
Precision、Recall、F1 或 mAP，也不承担产品门禁、批准基线或发布认证。

### 准备 suite

私有 suite 和视频放在仓库外。仓库只提供
`accuracy/schema/` 和 `accuracy/examples/` 下的 schema 与脱敏示例。可从旧式目录生成草稿：

```bash
node src/accuracy-cli.js init-suite \
  --input-root /private/dataset \
  --output /private/suite-draft \
  --target-chip bm1688
```

草稿不会创建伪 taskConfig，也不会猜算法 ID 或计划 ID。补全这些设备必需值后，把
`suite.draft.yml` 改名为 `suite.yml`。每个 case 需要相对视频路径、SHA-256 和事件期望：

```json
{"id":"helmet-positive-0001","task":"no-helmet","file":"videos/no-helmet/positive-0001.mp4","sha256":"<sha256>","expectation":{"minEvents":1},"tags":["quick"]}
```

正检通常使用 `minEvents: 1`，误检通常使用 `maxEvents: 0`。suite 不需要 gates 或
trial policy；旧 suite 中这两个字段仍可保留，但运行时忽略。

### 检查与运行

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

也可以用 `--user <account> --password-stdin`。命令不接受明文 `--password`。

`doctor` 只预检实际选中的样本，并检查视频可读性、哈希、设备登录、算法、计划、上传空间
和 Event/Page。设备上已有的 `acc-*` 对象只产生警告，不阻止使用新唯一通道开始测量。

每个样本取得一个有效结果。基础设施 ERROR 可按 suite 默认值重试，但算法 FAIL 不会自动
再跑第二轮；FAIL 本身就是要统计的数据。命中率只使用有效 PASS/FAIL，ERROR 单独显示。
完整测量即使含 FAIL 也退出 0，只有无法完成
测量或 trial 严格清理失败才退出 2。

`--profile quick` 选择带 `quick` tag 的代表样本，`--profile full` 运行完整选集。
`--case`、`--task` 和 `--tag` 可直接过滤。并发可设为 1、2 或 4；它只并行 CV case，
VLM 始终等 CV 完成后逐个执行。并发不会改变结果资格，因为工具没有资格/基线概念。

trial 使用唯一通道，等待 Decode/Detector readiness，在观察窗口内从 Event/Page 读取事件，
按通道、算法和时间再次过滤并去重。达到不可逆结论时可提前结束，最终仍等待事件集合收敛。
每个 trial 都删除自己的任务和通道并反查；该清理失败会使该 case 为 ERROR。

### 结果

每次运行输出：

| 文件 | 说明 |
| --- | --- |
| `run.private.json` | 完整 case、trial、事件、实际配置 hash 和清理结果 |
| `run.partial.json` | 可恢复的限频原子断点；完成后保存结果 hash |
| `summary.json` | 脱敏后的样本、算法、配置、micro/macro 指标和耗时 |
| `report.html` | 可离线查看的结果报告 |
| `integrity.json` | 已生成文件和图片的 hash 清单 |
| `artifacts/alerts/` | 有告警图片时按内容 hash 保存 |

`run.private.json` 和 `summary.json` 先保存。摘要验证、HTML 或 integrity 生成失败只警告，
不会抹掉已经完成的测量。summary 不包含密码、token、设备地址、SN、内部通道 ID、绝对路径
或告警 URL。

### 对比与阈值诊断

比较同一组选中视频的多次测量：

```bash
node src/accuracy-cli.js compare \
  --reference /private/runs/reference/summary.json \
  --candidate /private/runs/algorithm-b/summary.json \
  --candidate /private/runs/concurrency-2/summary.json \
  --output /private/comparisons/algorithm-and-speed
```

对比只要求选中的视频样本一致。算法 ID、实际 taskConfig hash、并发、软件版本、source mode、
目标芯片和工具版本都作为变化维度显示，不作为拒绝比较的资格条件。输出为
`comparison.json`、`report.html` 和 `integrity.json`。

`render` 可从 `summary.json` 离线重绘报告。`diagnose-threshold` 可对任意已测得的 FAIL
case 扫描 suite 明确列出的阈值；每个值运行三次以显示稳定或波动情况，但不会修改 suite、
设备默认配置或正式测量结果。

`local` 直接上传原始视频，是默认路径。`rtsp-deterministic` 是可选的摄取链路专项；
比较工具会明确显示 source mode 的差异，而不会把它变成产品资格限制。

## 当前限制

- 当前主要验证 `local` 视频模式。
- 报告为静态 HTML，暂不包含资源曲线图。
- 事件、MQTT、HTTP 推送等业务闭环校验尚未纳入默认判定。
- `metrics.json` 体积较大，适合程序分析，不建议人工直接阅读。
