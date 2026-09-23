# 平台托管接口（Management v1）

本接口用于平台向边缘端发布模型、场景、通道、时间模板和任务。当前属于开发分支扩展；CPU 回归与设备推理验收是不同层次，升级前仍需验证对应芯片、模型、视频源及断电恢复。旧有 Web、GB28181、ONVIF 和 MQTT 接口继续保留。

## 接入和身份

平台直接访问盒子 HTTP 服务，复用 `/gtw/cwai/login/dologin` 登录及 `mtk` 会话。管理接口只接受认证后的 HTTP 请求，不能通过 MQTT 或请求 JSON 伪造账号身份。

前缀为 `/gtw/cwai/Management/`。除 `UploadChunk` 使用 PUT 二进制，其余接口使用 POST JSON。沿用 `resCode`、`resData` 和 `resMsg` 响应结构；必须同时判断业务状态和资源回读，不能仅检查 HTTP 200。

首个合法写入绑定 `platformId` 和认证账号。更换登录会话不改变归属；不同平台不能静默接管。管理日志位于配置目录下的 `management/state.sqlite`，普通重启保留 `incarnation`，恢复出厂或丢失管理日志会生成新代次。

| 接口 | 主要输入 | 结果 |
| --- | --- | --- |
| `Capabilities` | 空对象 | 协议版本、SN、可识别芯片、运行时、代次及能力 |
| `ResourceInventory` | platformId、resourceIds | 所有保留版本的外部 ID、本地 ID、摘要、配置及任务状态 |
| `PrepareResource` | hash、size、name | 校验后的已有文件，或 uploadId 与续传偏移 |
| `UploadChunk` | 字节；X-Upload-Id、X-Upload-Offset | 已落盘偏移；结束时返回实际 SHA-256 |
| `ApplyOperation` | 操作对象 | PENDING / SUCCEEDED / FAILED |
| `OperationStatus` | operationId | 持久操作结果；中断操作继续核对 |
| `TaskActivate` | operationId、externalId、versionId、incarnation、enabled | 切换逻辑任务版本或全部停用 |

Sophon 通用固件不会把编译期的 BM1688 标签当作硬件测量结果。无法辨认芯片时返回 `unknown` 并拒绝模型下发，避免混用 BM1688 与 CV186X 产物。

芯片识别读取 `/proc/device-tree/` 和 `/sys/firmware/devicetree/base/` 下的 `compatible` 与 `model`，兼容大小写及 NUL 分隔的设备树字段。部分 BM1688 系统的 `compatible` 为通用的 `cvitek,cv181x`，需要从 `model` 中的明确 BM1688 标识识别；不能将 `cv181x` 本身映射成 BM1688。字段缺失、未识别或不同来源的 BM1688/CV186X 标识冲突时继续返回 `unknown`。能力查询和模型接收使用同一识别逻辑。

如果平台选择设备后提示“设备尚未确认芯片型号”，应检查 `Capabilities.chip`，并升级包含上述识别修复的盒子后端。仅更新盒子网页、刷新平台页面或修改管理地址不会改变旧后端的识别结果。升级后在平台重新检测设备，确认 `chip` 为实际芯片、`runtimes` 含 `bmrt`；完整参数下发还需要 `features` 含 `model-configuration-v1`。CPU/文件夹测试不替代实机升级后的接口和模型验收。

## 发布对象

`ApplyOperation` 包含 `operationId`、`platformId`、`externalId`、`versionId`、`hash`、`incarnation`、Unix 秒 `expiresAt`、`action`、`kind`、`name`、`config`、`files`、`references` 和 `activationPolicy`。

- 操作 ID 与完整载荷绑定；同 ID 不同载荷拒绝，重试返回已保存结果。
- 版本不可变，修改配置、文件或依赖必须创建新版本。
- 每个直接依赖包含 kind、externalId、versionId、localId，且必须已就绪。
- 文件先按摘要接收，名称只能用于格式识别和展示，不能指定任意落盘路径。
- 删除使用 `action: remove`，只删除指定版本；有其他版本或本地任务引用时拒绝级联删除。
- 中断操作会阻止后续写入，先查询其 OperationStatus 完成核对再继续发布。

## 配置转换

| 类型 | 配置约定 | 原生实现 |
| --- | --- | --- |
| model | chip、runtime、原生 modelType、config | 复用模型模板、文件及张量元数据校验；独立编号 |
| scene | edgeLayout 中原有 algorithmProcessdata、atomicList、algorithmMetadata JSON 字符串 | 复用布局保存和运行时加载校验 |
| channel | type、url 或 devicePath；本地视频放在 files | 原生通道服务与真实通道 ID |
| schedule | periods: week、begin、end；0=周日，1–6=周一至周六 | 原生时间模板，写盘失败不返回成功 |
| task | overrides、roi.areas、roi.shieldedAreas；依赖通道、场景和可选模板 | 参数归属校验与任务构建，先准备后启用 |

模型类型必须使用模板名称，例如 `yolov8_det`、`classify`、`feature`、`ocr`、`dino`。泛称 detector/classifier 不会被猜测成具体网络。模型 config 当前支持 normalizationMode、colorChannel 和 artifactRoles，未知字段明确拒绝。artifactRoles 将文件名映射为 model、encoder、decoder、vocab、tokenizer、characters 等原生角色。模型字节必须匹配运行时，不能靠改扩展名转换。

使用 `model-configuration-v1` 能力时，可在模型载荷的同级 `nativeConfig` 中提交原配置页 JSON。接收端在实际文件生成的原生配置上合并各模型的 `params`、`labels` 和生成模型的 `config.generation`；输入/输出节点必须与实际模型完全一致（原页面也是只读）。本地模型编号、文件名、摘要和其他原生字段保持接收端值。芯片、模型类型、节点数量或张量不一致会失败。配置原子写入并同步后才写入模型就绪标记。

旧固件不声明此能力，平台必须在预检和执行时检查，不能发送后假定参数已生效。模型包导入及既有预置资源纳管不包含在此能力中。

通道支持 RTSP、USB、本地视频和 GB28181 源。ONVIF 支持已解析的 RTSP 地址，或通过 `config.onvif` 提供原有保存参数（endpoint、username、password、profileToken 等）；完整配置保存为独立、可重试的源。GB28181 仍需对应 SIP 设备注册和源就绪。

任务版本使用独立执行场景保存参数，模型通过版本引用共享。`activationPolicy: prepare` 不会启用任务。启用新版先停旧版，停用关闭同一逻辑任务的全部版本。时间窗口外报告 scheduled，启动中报告 starting，初始化失败不会报告 running；平台必须通过 Inventory 确认最终状态。

## 存储和恢复

接收端限制单片 1 MiB、单文件 5 GiB、每账号最多 8 个未完成会话，24 小时失效。入库前校验 SHA-256，磁盘检查包含未完成会话预留，并保留至少 256 MiB 可用空间。完成文件按摘要去重；原生模型或视频文件是执行副本，不是第二份平台资源主数据。

SQLite 先落盘操作意图和预留编号，再调用原生服务；中断后使用同一编号恢复，避免重复创建。文件同步、数据库提交失败不会伪装成成功。模型完成导入标记后才可就绪。资源回读检查原生状态，不能把历史成功回执当作当前事实。

当前保留历史版本、操作记录和已完成内容文件，不自动回收；长期保留策略与容量需要按实际规模验证。盒子上的人工配置修改会造成漂移，应回到平台重新发布并核对，不要直接编辑管理数据库。

## 验证入口

运行 `bash scripts/build_cpu_test.sh`、`./build_cpu/cosmo-tests "[management]"`、完整 `./build_cpu/cosmo-tests`、`bash scripts/format_check.sh --staged --check` 和 `npm run docs:verify`。

新增用例覆盖协议恢复、幂等、归属冲突、版本隔离、依赖保护、分片校验、真实 HTTP PUT，以及原生任务准备、时间模板落盘和 ONVIF 持久化。测试替身仅用于故障注入；真实设备视频、模型推理、升级回滚和断电长稳另行验收。
