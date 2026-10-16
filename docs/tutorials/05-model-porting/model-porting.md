---
title: 第三方模型接入：转换、上传与验证
description: 确认第三方模型的支持条件，完成转换、上传、配置、运行和端到端验收。
prev:
  text: 算法编排
  link: /tutorials/04-pipeline-orchestration/pipeline-orchestration
next: false
---

# 第三方模型接入：转换、上传与验证

| 项目 | 说明 |
| --- | --- |
| 适合谁 | 需要把自有检测或分类模型接入 CosmoEdge 的算法工程师和集成开发者 |
| 完成后能做什么 | 判断模型是否满足运行条件，转换并上传模型，配置解析参数，完成图片、视频和持续运行验证 |
| 使用前提 | 已理解 Pipeline；掌握模型输入、输出、预处理、后处理和标签顺序 |
| 预计时间 | x86 ONNX 路径约 40–60 分钟；Sophon 或 Rockchip 转换路径通常需要额外 30–60 分钟 |
| 是否需要设备 | x86 路径需要 ONNX Runtime 版 CosmoEdge；Sophon 与 Rockchip 路径需要目标芯片设备及匹配转换工具链 |
| 最终验收结果 | 模型可加载、推理输出可解析、图片与视频结果正确，并在目标设备上持续运行无资源错误 |

第三方模型接入按以下顺序完成：

1. 确认支持条件和模型契约。
2. 导出 ONNX；Sophon 设备转换为目标芯片的 `bmodel`，Rockchip 设备转换为目标芯片的 `rknn`。
3. 在转换主机上检查产物。
4. 上传并配置模型。
5. 先做正负图片验证。
6. 接入视频 Pipeline。
7. 验证模型加载、结果解析、事件和持续运行。

“文件上传成功”只证明文件被接收，不证明算子、输入形状、输出布局和后处理与 CosmoEdge
兼容。

## 1. 确认支持条件

### 1.1 当前后端和文件格式

| 目标后端 | “添加模型”接受的文件 | 模型包导入时的主文件 | 当前运行方式 | 设备条件 |
| --- | --- | --- | --- | --- |
| x86 CPU | `.onnx` | `model.onnx` | ONNX Runtime CPU | x86_64 主机和对应 CosmoEdge 构建 |
| Sophon | `.bmodel` | `model.nn` | Sophon BMRT | BM1688 或 CV186X，转换产物必须匹配芯片 |
| Rockchip RKNN | `.rknn` | `model.rknn` | RKNN Runtime | RK3576 或 RV1126B，转换产物必须匹配实际芯片 |

`model.nn` 是 CosmoEdge 模型包中的内部文件名，封装的是设备侧模型；通过页面单独添加
Sophon 模型时应选择 `.bmodel`，不要把文件扩展名手工改成 `.nn`。

PyTorch `.pt`、TensorFlow SavedModel 或其他训练框架产物不能直接上传。它们必须先导出
为 ONNX；Sophon 还要使用匹配工具链把 ONNX 转为目标芯片的 `.bmodel`，Rockchip 则转换
为目标芯片的 `.rknn`。RK3576 与 RV1126B 的 `.rknn` 不是可互换模型。

### 1.2 格式之外还要匹配的契约

| 契约 | 接入前必须知道 |
| --- | --- |
| 模型类型 | 检测、分类、关键点、特征或其他；页面子类型决定使用哪种解析器 |
| 输入 | 名称、数据类型、形状、批量、动态维度是否固定 |
| 预处理 | RGB/BGR、缩放方式、补边颜色、归一化均值和缩放系数 |
| 输出 | 张量名称、形状、维度顺序、是否已包含 NMS |
| 后处理 | 模型家族、置信度、NMS/IoU、坐标格式和最大保留数 |
| 标签 | 类别 ID 与名称的精确顺序 |
| 资源 | 模型文件大小、运行内存、并发路数和目标帧率 |
| 许可 | 模型权重、训练数据和导出工具是否允许目标使用和分发方式 |

CosmoEdge 当前已有 `YOLOV8_DET` 等解析路径，但“任意 ONNX”并不自动兼容。自定义输出、
内置 NMS、动态形状或未支持算子可能需要新的解析器或运行时代码。

### 1.3 已验证能力与条件性兼容

- **由当前代码直接支持**：x86 添加 `.onnx`、Sophon 添加 `.bmodel`，以及模型包中的
  `model.onnx` / `model.nn`；RKNN 构建添加 `.rknn`，模型包中使用 `model.rknn`。
- **仓库中已有参考证据**：YOLOv8 检测模型在 x86 ONNX 路径完成过模型导入、实时叠加
  和事件输出。
- **仍需在目标候选版本上验证**：你的具体模型、Sophon 转换产物、性能、资源占用、
  多路并发和长期稳定性。
- **不能仅凭格式承诺**：其他 ONNX 模型家族、其他输出布局或未经验证的芯片/量化组合。

## 2. 可复现实例：x86 YOLOv8n 人员检测

本例使用固定 Ultralytics 版本导出公开的 YOLOv8n 权重，在
`data/test-video/Safety Helmet.mp4` 中检测 COCO 类别 `person`。它验证单阶段目标检测接入，
不等同于“未戴安全帽”分类任务。

### 2.1 准备固定环境和模型

参考环境：

| 项目 | 版本 |
| --- | --- |
| Python | `3.13.11` |
| Ultralytics | `8.2.84` |
| ONNX | `1.20.1` |
| ONNX Runtime | `1.26.0` |
| 导出输入 | `1 × 3 × 640 × 640` |

创建隔离环境：

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install \
  "ultralytics==8.2.84" \
  "onnx==1.20.1" \
  "onnxruntime==1.26.0"
```

下载固定发布资产并记录来源文件哈希：

```bash
curl -L \
  -o yolov8n.pt \
  https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n.pt
sha256sum yolov8n.pt
```

macOS 可用 `shasum -a 256 yolov8n.pt`。

导出并记录 ONNX 哈希：

```bash
yolo export \
  model=yolov8n.pt \
  format=onnx \
  imgsz=640 \
  batch=1 \
  dynamic=False
sha256sum yolov8n.onnx
```

保留命令输出、Python 和包版本、源权重哈希与 ONNX 哈希。即使文件名相同，哈希不同也应
视为不同候选模型。

### 2.2 转换前检查

在仓库根目录使用统一检查脚本，让 ONNX checker 和 ONNX Runtime 完成一次零输入加载与推理：

```bash
python tools/check_onnx_model.py yolov8n.onnx
```

动态输入可重复传入 `--shape images=1,3,640,640`；需要机器可读记录时使用
`--json <输出路径>`。脚本会记录实际依赖版本和模型 SHA-256，不保存推理张量。

通过标准：

- `onnx.checker` 无错误；
- ONNX Runtime 能创建会话并执行一次推理；
- 输入是预期的 `1 × 3 × 640 × 640` 浮点张量；
- 输出形状与实际导出日志一致。

零输入测试只验证图可加载，不验证检测准确性。

### 2.3 准备模型元数据

本例采用 YOLOv8 原始检测输出：

| 配置 | 本例值 |
| --- | --- |
| 主类型 | 检测算法 |
| 子类型 | `YOLOV8_DET` |
| 输入尺寸 | `[640, 640]`，顺序按页面的“高、宽”说明 |
| 缩放 | 等比缩放并居中补边 |
| 补边色 | `114, 114, 114` |
| 颜色 | RGB |
| 归一化 | `0–1`，缩放系数约 `1/255` |
| 输出 | YOLOv8 原始检测张量，由 CosmoEdge 执行阈值和 NMS |
| 标签 | COCO 80 类原始顺序；本例在 Pipeline 中只启用 ID `0` 的 `person` |

从导出模型打印标签，避免手工重排：

```bash
python - <<'PY'
from ultralytics import YOLO
for class_id, name in YOLO("yolov8n.pt").names.items():
    print(f"{class_id}\t{name}")
PY
```

如果实际 ONNX 输出已经包含 NMS、形状不是导出记录中的 YOLOv8 原始布局，或者标签数量
不一致，应停止上传并修正导出或实现匹配解析器。

::: tip 关于下方旧版 VisDrone 截图
为保留完整操作路径，本页恢复了旧版 VisDrone 示例的全部界面截图。它的十个标签是
`pedestrian`、`people`、`bicycle`、`car`、`van`、`truck`、`tricycle`、
`awning-tricycle`、`bus`、`motor`，不能复制给本页的 COCO YOLOv8n。若复用 VisDrone，
还需固定模型来源、版本、哈希、输入输出和许可；截图本身不是可复现性证据。
:::

## 3. Sophon 路径：把同一 ONNX 转为 bmodel

仅在目标设备为 Sophon 时执行本节。转换工具版本、目标芯片和模型候选必须一起记录。

如果把任务交给编码智能体，先阅读[智能体辅助二次开发](/development/agent-assisted-development)。
普通用户只需说明目标设备、模型物料、业务偏好和期望交付。智能体应生成本次运行的任务契约，
先执行 `python3 tools/agent_workflow.py start` 复制所指物料、生成任务记录并选择路径；任务或授权变化后重跑
`python3 tools/agent_workflow.py assess`，再在实际 Linux 执行环境运行 `python3 tools/agent_workflow.py doctor`。环境满足后，
通过 `python3 tools/conversion_workflow_dispatch.py convert` 和 `python3 tools/conversion_workflow_dispatch.py verify` 留下工具链、命令、哈希与分层证据。用户无需手工选择版本、
镜像或命令；下面的内容仅是高级人工执行和排障参考。

如果用户给出隔离 Linux 开发机的地址、账户、密码并明确要求连接，这已经是本任务的远程执行
确认。智能体应生成脱敏记录并通过 `python3 tools/remote_access.py` 的 OpenSSH 交互提示使用密码，不重复
要求 SSH Key 或授权表；安装、提权和设备写入仍在实际需要时单独确认。

当前正式执行器从 ONNX 开始。`.pt`/`.pth` 可作为待评估物料，但训练框架导出 ONNX 必须作为独立
阶段请求或评估；路径评估未就绪时，`doctor`、转换和证据链不会放行。不要先安装 Ultralytics、
临时导出一次，再把该过程写成仓库已经支持的固定能力。

先把“官方支持的安装路径”“本机是否具备能力”和“本次实际身份”分开。执行时应以算能
[TPU-MLIR 官方安装说明](https://github.com/sophgo/tpu-mlir#-installation)为准：其支持的 wheel、
源码或预构建 Docker 路径都可以进入候选，不要求与本页某个精确版本或目录布局一致。候选路径
只有在 `tpu_mlir` 可导入、转换入口可调用、运行库完整且实际候选可预检时才是 `READY`；准入后
再冻结包版本、镜像 ID/摘要、Python、命令路径与哈希。

Sophon 转换工具链按官方路径运行在 Linux 环境。Windows 可以用于智能体编排和物料整理，但应把
本节转换路由到隔离的 Linux x86_64 开发环境。兼容层只有在用户明确接受风险时才作为实验路径。

官方开发镜像只代表基础执行环境；镜像中是否已经包含完整编译器，要以实际能力检查为准。拉取、
启动和安装仍需单独授权，且必须记录解析后的镜像身份。下方 v3.2 镜像是仓库的可复用示例，
不是所有任务的唯一准入版本；其他上游支持路径仍按实际可调用能力评估。

### 3.1 安装并验证 Docker

根据目标操作系统使用 [Docker Engine 官方安装说明](https://docs.docker.com/engine/install/)。
不要把旧教程中的单条 `apt` 或 `yum` 命令当作所有系统都适用的安装方法。

![旧版教程中的 Docker 安装过程示意](images/img_01.webp)

安装后检查客户端和守护进程：

```bash
docker --version
docker info
```

![通过 docker --version 检查 Docker 安装](images/img_02.webp)

Linux 若希望普通用户执行 Docker，可按官方安装后步骤配置 `docker` 用户组，然后重新登录；
该用户组等同于授予较高主机权限，应遵循本机安全策略。

![旧版教程中的 Docker 用户组配置示意](images/img_03.webp)

### 3.2 下载并载入固定转换镜像

下面命令对应仓库现有参考工具链 v3.2。先下载并记录压缩包哈希：

```bash
curl -L \
  -o sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz \
  https://sophon-file.sophon.cn/sophon-prod-s3/drive/24/06/14/12/sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
sha256sum sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
```

![下载固定版本的 Sophon TPU-MLIR 镜像压缩包](images/img_04.webp)

载入并确认镜像标签：

```bash
docker load -i sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
docker images sophgo/tpuc_dev
```

![使用 docker load 载入 Sophon 转换镜像](images/img_05.webp)

![转换镜像载入成功后的终端输出](images/img_06.webp)

常用命令：

| 命令 | 作用 |
| --- | --- |
| `docker images` | 查看本地镜像和标签 |
| `docker run --rm -it -v "$PWD:/workspace" sophgo/tpuc_dev:v3.2 bash` | 启动固定镜像并挂载当前目录 |
| `exit` | 退出容器；使用 `--rm` 时同时删除已停止的临时容器 |

### 3.3 启动容器并确认工具

将 `yolov8n.onnx` 放在当前目录后启动容器：

```bash
docker run --rm -it \
  -v "$PWD:/workspace" \
  -w /workspace \
  sophgo/tpuc_dev:v3.2 \
  bash
```

![挂载模型目录并启动 Sophon 转换容器](images/img_07.webp)

镜像应已包含转换工具。先用 `model_transform --help`、`model_deploy --help` 和版本信息确认；
只有固定镜像确实缺少工具时才安装额外包，并记录包版本和来源，不要无条件覆盖镜像环境。

![旧版教程中的 TPU-MLIR 环境检查界面](images/img_08.webp)

### 3.4 转换为 MLIR

以下命令面向固定的 v3.2 参考镜像；升级工具链时先查看对应命令的 `--help`。在容器内执行：

```bash
cd /workspace
model_transform \
  --model_name yolov8n \
  --model_def yolov8n.onnx \
  --input_shapes '[[1,3,640,640]]' \
  --pixel_format rgb \
  --mlir yolov8n.mlir
```

![旧版 VisDrone 示例执行 model_transform 的命令位置](images/img_09.webp)

![model_transform 完成并生成 MLIR 的终端输出](images/img_10.webp)

若模型需要显式输出名、均值、缩放或测试输入，应以实际导出契约和该版本工具帮助为准，
不能照抄截图中的 VisDrone 参数。

### 3.5 编译为目标芯片 bmodel

BM1688 F16 示例：

```bash
model_deploy \
  --mlir yolov8n.mlir \
  --quantize F16 \
  --chip bm1688 \
  --model yolov8n_bm1688_f16.bmodel

model_tool --info yolov8n_bm1688_f16.bmodel
sha256sum yolov8n_bm1688_f16.bmodel
```

通过较新 ONNX 环境完成 x86 预检，不代表同一文件一定被所选 TPU-MLIR 接受。应结合
[ONNX 版本规则](https://onnx.ai/onnx/repo-docs/Versioning.html)和
[ONNX Runtime 兼容性说明](https://onnxruntime.ai/docs/reference/compatibility.html)，再用本次冻结的
工具链检查实际候选的 IR、opset 和算子；不兼容时应从源权重重新导出受支持的 ONNX，不得直接
篡改 `ir_version` 冒充兼容。

![旧版 VisDrone 示例执行 model_deploy 的命令位置](images/img_11.webp)

![转换完成后生成 bmodel 文件](images/img_12.webp)

CV186X 必须使用支持该芯片的工具链和芯片参数，不能把 BM1688 产物上传到 CV186X 设备。
如果工具报告未支持算子、输出不一致或编译失败，转换没有完成；更换文件扩展名不能解决。

### 3.6 转换后校验

先检查模型元数据：

```bash
model_tool --info yolov8n_bm1688_f16.bmodel
```

再使用工具链提供的模型运行器和同一张固定测试图片，对比源框架、ONNX 与 bmodel 的框、
类别和分数。转换证据至少包括：

- 工具链版本、芯片参数和完整命令；
- ONNX、MLIR 和 `.bmodel` 哈希；
- 输入形状、输出张量与模型信息检查；
- 有测试输入时，按用户覆盖容差或当前工具默认容差完成转换前后张量比对并记录策略；
- 获得设备授权后，同一张图片在源框架、ONNX 和目标设备上的框、类别与分数对比；
- F16 或量化造成的精度差异。

智能体执行路径会把这些结果写入当前运行的 `execution-manifest.json` 和 `evidence.md`。
每次重跑都会把上次清单归档到私有运行目录；新一轮 `UNVERIFIED` 不会抹掉之前的失败，只有新的
实测 `PASS` 或用户明确确认并记录理由的豁免可以解释后续结论。远程执行时还要记录脱敏的数据流
状态，但不能保存传输凭据。
只有使用固定工具链完成两次真实录制、通过张量比对、状态为 `conversion-verified`、生命周期为
`active` 且印章仍然有效，记录才可作为官方实例使用。短码只是印章引用；被标记为 `revoked` 的
实例保留历史录制和印章，但不得再用于选择或兼容性声明。普通候选仍可按自己的任务目标交付，
但不得借用其他实例的固定形状或哈希宣称成功。

Sophon 添加模型页面会要求 `.bmodel` 文件，实际页面见下一节的添加模型表单。

## Rockchip RKNN 路径：共享后端、目标专用产物

RK3576 与 RV1126B 共用同一套 CosmoEdge RKNN 推理实现、Rockchip 媒体接口和模型契约；
芯片差异由 `config/rknn/platforms/<chip>.json` 平台 profile 提供。模型 spec 不写死芯片，
但每次转换必须绑定一个 profile，因此输出的 `.rknn` 仍是目标专用产物，不能跨芯片复制使用。

智能体辅助流程统一使用 `python3 tools/conversion_workflow_dispatch.py convert` 与 `python3 tools/conversion_workflow_dispatch.py verify`。执行器按任务合同选择
RKNN Toolkit2，冻结 Python、wheel、平台 profile、模型 spec、校准集和产物哈希。RK3576 与
RV1126B 不各自维护一套转换脚本。手工排障时可参考
[RK3576 RKNN 开发说明](/guide/rk3576-rknn-development)，并把平台参数换成实际目标：

```bash
python tools/rknn/convert_model.py \
  --spec config/rknn/models/yolov8.json \
  --platform-profile config/rknn/platforms/rv1126b.json \
  --model yolov8-heads.onnx \
  --output yolov8-rv1126b-int8.rknn \
  --quantize --dataset calibration/dataset.txt
```

转换主机通过不等于设备验收。必须在对应设备上继续验证 Runtime/驱动、数值输出、图片与视频
后处理、OSD、规则、告警、5 FPS 目标以及稳定性；这些结果应绑定目标芯片和产物 SHA-256。

## 4. 上传并配置模型

### 4.1 进入模型仓库

打开 **模型仓库**，在列表中可以搜索模型、查看类型和关联任务。

![模型仓库列表和添加模型入口](images/img_13.webp)

点击 **添加模型**。**导入模型** 用于完整模型包，不是本节的单文件上传路径。

![从模型仓库进入添加模型页面](images/img_14.webp)

### 4.2 填写并上传

1. 主类型选择 **检测算法**。
2. 子类型选择与输出解析契约一致的 `YOLOV8_DET`。
3. 填写可区分候选版本的模型名称和说明。
4. 选择归一化方式和颜色通道。
5. x86 上传 `yolov8n.onnx`；Sophon 上传目标芯片对应的 `.bmodel`。
6. 保存。

![旧版 VisDrone bmodel 的添加模型表单示意](images/img_15.webp)

::: warning 截图与本例的差异
截图使用 Sophon VisDrone 模型，因此显示 `.bmodel` 和 VisDrone 名称；x86 本例应上传
`yolov8n.onnx`。两种后端都必须使用与实际产物相符的预处理和标签。
:::

上传完成后确认条目出现在列表，并记录系统分配的模型 ID。此时仍不能判定推理可用。

![新模型出现在模型仓库列表中](images/img_16.webp)

### 4.3 配置输入、后处理和标签

打开模型 **配置**，逐项与导出记录核对：

- 输入尺寸和缩放、补边方式；
- RGB/BGR 和归一化；
- 检测置信度和 NMS 阈值；
- 最大保留目标数；
- 类别 ID、名称和顺序；
- 页面显示的输出或高级配置。

![配置模型输入尺寸、置信度和 NMS 参数](images/img_17.webp)

![配置模型类别 ID、阈值和标签名称](images/img_18.webp)

保存后再次进入配置页，确认值已持久化。截图中的 VisDrone 十类只适用于对应模型；本页
YOLOv8n 必须保留 COCO 80 类原始顺序，视频 Pipeline 中再只启用 `person`。

## 5. 图片验证：先证明加载和解析

图片路径能把视频解码、跟踪和事件规则排除在外，适合先定位模型与解析问题。

### 5.1 创建图片分析任务

进入 **任务配置**，点击 **新建任务**。

![任务配置列表中的新建任务入口](images/img_19.webp)

填写：

- 任务名称：`YOLOv8n 人员图片验证`；
- 数据源类型：**图片分析**；
- 任务类型：**检测/分析**。

![创建图片分析任务并填写基础信息](images/img_20.webp)

保存后确认任务出现在列表，状态正常。

![新建的图片分析任务出现在任务列表](images/img_21.webp)

### 5.2 编排最小图片链路

点击任务的 **算法编排**。

![从图片分析任务进入算法编排](images/img_22.webp)

业务流程中只添加 **目标检测算法**，保持因果链最短。

![图片分析任务中的目标检测算法节点](images/img_23.webp)

选择刚上传的 YOLOv8n，只启用 `person` 标签，然后保存。

![在目标检测节点中选择第三方模型和标签](images/img_24.webp)

### 5.3 上传正负样本并分析

打开 **图片分析**，选择 `YOLOv8n 人员图片验证`。

![在图片分析页面选择刚创建的算法](images/img_25.webp)

至少准备：

- 一张人员清晰、尺寸足够的正样本；
- 一张不含人员的负样本；
- 可选：小目标、遮挡和边缘位置样本。

上传图片。

![图片上传后处于待分析状态](images/img_26.webp)

点击 **开始分析**，等待状态变为已完成。

![图片分析完成并显示检测类别](images/img_27.webp)

打开结果详情，检查框、类别、置信度和结果列表。

![图片分析结果详情中的框、类别和置信度](images/img_28.webp)

通过标准：

- 模型初始化没有报错；
- 正样本的人员框位置合理，类别显示 `person` 而不是错误 ID；
- 负样本没有大量人员误框；
- 置信度是有限且合理的数值，不是空值、NaN 或固定异常值。

图片验证失败时不要继续创建视频任务。

## 6. 接入视频 Pipeline

### 6.1 创建视频分析任务

回到 **任务配置** 新建任务。

![新建视频分析任务的基础信息表单](images/img_29.webp)

填写：

- 任务名称：`YOLOv8n 人员检测验证`；
- 数据源类型：**视频分析**；
- 任务类型：**检测/分析**。

保存并确认新任务出现在列表。

![视频分析任务创建成功并出现在列表](images/img_30.webp)

### 6.2 搭建最小可验收链路

点击 **算法编排**，从空白流程开始。

![新视频任务的空白算法编排页面](images/img_31.webp)

按顺序配置：

1. **视频解码**。
2. **目标检测算法**：选择上传的 YOLOv8n，只启用 `person`。
3. **追踪算法**。
4. **类别过滤**：保留 `person`，最小行人尺寸可从 `60` 起步。
5. **区域告警判断**：输入主区域，配置检测时间。
6. **事件上报**：首次验证保留抓拍图。

![在视频 Pipeline 中选择第三方模型、标签和取帧频率](images/img_32.webp)

![在第三方检测后连接追踪节点](images/img_33.webp)

![旧版数量限制区域规则的界面位置示意](images/img_34.webp)

::: warning 当前区域规则与旧截图不同
旧截图展示“数量限制”区域规则，适合离岗或聚集类业务；本页人员入区验证应使用当前
**区域告警判断** 的主区域与检测时间语义。不要把旧截图的数量阈值机械复制到入区链路。
:::

![在 Pipeline 末端添加事件上报](images/img_35.webp)

在 **参数配置** 复核模型、追踪、区域规则和事件字段，再保存。

![旧版详细参数页中的模型追踪和区域规则字段](images/img_36.webp)

当前界面没有必须手工添加的独立 OSD 节点；实时框由结果元数据和展示端渲染。先验证
“视频解码 → 目标检测”能稳定出框，再逐个恢复跟踪、过滤、区域与上报节点，便于定位问题。

## 7. 端到端验收

### 7.1 添加测试通道

准备一段含人员的测试视频。可直接使用仓库中的
`data/test-video/Safety Helmet.mp4`。进入 **视频接入**，添加 **离线视频** 通道。

![添加离线视频通道并上传测试视频](images/img_37.webp)

::: warning 上传容量以当前界面为准
截图来自旧版，其中“最大 1GB”不是当前固定限制。当前上传采用分片和安全空间检查；
是否接纳由设备当时的安全可用空间决定，容量不足时按界面给出的所需空间、可用空间和
建议动作处理。
:::

等待上传和通道处理完成，再点击 **服务分配**。

![离线视频通道创建完成后的服务分配入口](images/img_38.webp)

### 7.2 分配任务、区域和运行策略

在场景任务列表中选择 `YOLOv8n 人员检测验证`。

![在服务分配页选择第三方模型验证任务](images/img_39.webp)

新增一个覆盖人员活动范围的检测区域；区域过小会造成“模型有框但不告警”。

![为验证任务绘制并保存检测区域](images/img_40.webp)

在 **运行策略** 中确保当前时间位于有效时段。离线视频播放次数允许填写 `0–100`：
`0` 表示无限循环，`1–100` 表示总播放次数。

![配置离线视频运行策略和播放次数](images/img_41.webp)

保存并启用服务。

### 7.3 检查实时推理

打开 **实时展示** 并选择测试通道。

![在实时展示中选择第三方模型测试通道](images/img_42.webp)

在算法叠加设置中选择验证任务。

![选择第三方模型任务的算法叠加](images/img_43.webp)

检查：

- 视频持续播放；
- 人员位置出现框；
- 类别为 `person`，分数和坐标随画面变化；
- 无人员片段不出现大量固定框；
- 跟踪开启时，同一目标的结果保持合理连续。

### 7.4 检查告警和事件中心

目标满足区域与检测时间规则后，应出现告警提示。

![第三方模型任务触发的告警弹窗](images/img_44.webp)

进入 **事件中心 → 检测/分析**，按任务和通道查询事件。

![事件中心中的第三方模型检测记录](images/img_45.webp)

确认事件抓拍图中的框、类别、通道、区域和时间正确。实时出框但无事件时，优先检查区域、
检测时间、运行策略和事件上报节点，不要先修改模型。

### 7.5 持续运行

至少让离线视频完整循环一次，并在项目验收中设置明确的持续运行窗口。期间记录：

- 进程是否重启或崩溃；
- 推理耗时和实际帧率是否稳定；
- 主机内存、设备内存和磁盘是否持续增长；
- 事件是否能持续解析，而不是只成功第一帧；
- 停止并再次启动任务后能否恢复。

生产使用还必须按目标并发路数、分辨率和运行时长重新做容量与稳定性验收。单路功能成功
不能证明生产容量。

## 8. 失败路径

### 上传成功但模型无法运行

1. 对比上传后文件哈希和导出产物。
2. 确认 x86 使用 ONNX、Sophon 使用目标芯片对应的 bmodel。
3. 查看第一个模型初始化错误：未支持算子、形状、文件损坏或资源不足。
4. 在转换主机重新执行 ONNX Runtime 或目标工具检查。
5. 确认 Pipeline 选择的是新模型 ID。

### 能运行但输出格式不匹配

典型表现是零目标、坐标越界、类别全部相同或置信度异常。按顺序核对：

1. 子类型是否选择正确解析器；
2. 输出名称、数量、形状和维度顺序；
3. 导出是否内置 NMS；
4. 坐标是 `xywh` 还是 `xyxy`，是否已经归一化；
5. 标签数量和顺序；
6. 预处理的 RGB/BGR、缩放、补边和归一化。

若输出契约与现有解析器不同，需要实现或适配后处理，不应通过随意改阈值掩盖。

### 资源不足

1. 停止其他模型任务，验证单模型最小链路。
2. 记录加载前后内存和设备内存。
3. 降低取帧频率不一定降低模型常驻内存；模型本身无法加载时应换更小模型或适当精度。
4. Sophon 量化或 F16 转换必须重新检查精度。
5. 按目标并发路数做容量准入，不要从单路结果线性外推。

### 图片正确但视频错误

检查视频预处理是否与图片路径一致、ROI 是否覆盖目标、取帧频率是否合理，以及跟踪、过滤
或事件规则是否删除了正确检测结果。把 Pipeline 暂时缩减到“视频解码 + 目标检测”，再
逐个恢复规则节点。

### 实时有框但没有告警

按“当前时间是否在运行策略内 → ROI 是否覆盖目标 → 区域规则是否匹配 → 检测时间是否已
满足 → 事件上报是否连接 → 服务是否启用”的顺序排查。图片分析成功不能证明事件规则已生效。

## 完成验收

- [ ] 候选模型有来源、版本、输入输出说明和 SHA-256。
- [ ] 文件格式、目标后端和设备芯片匹配。
- [ ] 转换前和转换后检查均通过。
- [ ] 模型配置与预处理、后处理和标签顺序一致。
- [ ] 正负图片样本验证通过。
- [ ] 视频 Pipeline 输出正确的框、类别和事件。
- [ ] 持续运行窗口内无崩溃、资源持续增长或解析中断。
- [ ] 验收记录绑定 CosmoEdge 版本、模型哈希、设备和配置。

完成以上八项，才表示“第三方模型接入”闭环通过；仅完成模型上传或单张图片出框不算最终
验收。
