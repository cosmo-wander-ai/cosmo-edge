---
title: CV186X 快速开始
description: 在已准备好的 CV186X Linux 设备上安装匹配版本的 CosmoEdge，并完成首次检测。
prev:
  text: 构建指南
  link: /guide/build
next:
  text: RK3576 / RKNN 集成
  link: /guide/rk3576-rknn-development
---

# CV186X 快速开始

本指南适用于已经配置好 Sophon 运行依赖和网络的 CV186X Linux 设备，使用 BMRT 与
匹配 CV186X 的 `.nn` 模型。v1.1 历史公开压测中的两份 Sophon 模型与 BM1688 压测产物
字节一致；其他模型是否可复用仍须按模型合同逐项验证。历史结果不代表当前新包已完成设备验收。

安装前必须先核对硬件身份，不能用设备 IP 或旧模型目录名代替平台证据：

```bash
tr -d '\0' </proc/device-tree/model; echo
```

设备树字符串是 SoC、加速器或固件身份，不一定等于整机的商业平台名称。采用 Sophon
算力栈的 CV186X 整机可能合法地报告 `BM1688`；不能仅凭这一行输出否决 CV186X。
验收必须同时绑定三层证据：

1. 供应商、BOM 或受控资产清单确认该整机属于 CV186X 产品平台；
2. 安装包的 `TARGET_CHIP` 为 `cv186x`，并且安装包 SHA-256 与发布记录一致；
3. 设备实际加载的模型 SHA-256 与 CV186X 资源记录一致，并在该设备上完成一次真实
   BMRT 推理或产品任务推理。

如果设备树为空且没有独立的平台材料，或设备树、资产材料、包标记和运行时证据互相
冲突，应停止资格验收并先修正身份。服务能够启动本身不等于 CV186X 推理已经通过。

## 1. 获取并核对安装包

在构建主机按[构建指南的 CV186X 入口](./build.md#cv186x)选择 Linux Bash 或 Windows
PowerShell。默认 Open 产物位于 `build_output/public-runtime/cv186x/`，目录同时包含
唯一安装包、`TARGET_CHIP` 和 `SHA256SUMS`。

也可以从 [GitHub Release](https://github.com/cosmo-wander-ai/cosmo-edge/releases) 获取所需
版本中明确标注为 CV186X 的包及校验信息。按[构建产物校验步骤](./build.md#将-sophon-构建包交给部署流程)
确认芯片为 `cv186x`、SHA-256 与清单或发布记录一致，并记录实际完整包名。
源码版本与发布包版本可以不同，后续安装不能使用固定旧版本的通配符选包。

## 2. 安装并启动

按[部署指南：SSH 安装路径](./deployment.md#ssh-安装路径)操作，将取包目录选为
`build_output/public-runtime/cv186x/`。该章节区分构建主机传输与设备安装，并统一维护
备份、校验、安装、重启和恢复步骤；使用上一步已核验的实际包名。

设备恢复后确认 `cosmo.service` 为 active，管理页面中的软件版本与目标包一致，再完成
[安装和升级后验收](./deployment.md#安装和升级后验收)。已有 CosmoEdge 的设备也可在
**系统管理 → 系统维护 → 软件升级** 上传同一个包。

## 3. 使用内置开源模型创建首个事件

CV186X Sophon Open 包由构建脚本根据 `cv186x` 型号自动选择对应资源目录，默认 `include` 包含 v1.1 历史公开压测
使用的两份模型；用户无需传入资源路径：

- `YOLOV8n V1.0.0`：人员检测，`1x3x640x640`，模型文件 7,023,600 B；
- `helmet V1.0.0`：安全帽分类，`1x3x224x224`，模型文件 6,001,416 B。

它们的模型目录、输入输出合同与 SHA-256 记录在
[ScenarioBench v1.1 模型身份表](/benchmarks/scenario-bench/v1.1/models/cv186x.json)。
模型子目录仍保留 `prod_BM1688_` 历史兼容前缀，这是为了完整保留 CV186X 压测设备中的
原始文件。目录名和设备树中的 `BM1688` 都不单独决定产品平台。CV186X 资格依据是受控的
整机平台映射、设备实际加载文件与 CV186X 资源目录副本的 SHA-256 完全一致，以及真机
推理成功；不能据此推断其他 BM1688 模型也可直接复用。

1. 登录 Web 控制台并进入 **模型仓库**，确认上述两份模型可见。
2. 在 **视频接入** 添加一路测试视频，再在 **场景任务** 使用人员检测模型创建任务并绑定
   该视频；需要安全帽完整链路时，在编排中增加安全帽分类模型。
3. 打开算法预览，确认 OSD 和事件；随后检查任务状态与服务日志无持续报错。将模型
   SHA-256、实际设备树字符串和该次推理结果一起写入验收记录。

导入自有模型时，目录至少应包含匹配的 `config.json` 和模型文件。输入尺寸、量化方式、
前后处理和输出张量必须与配置一致；详细合同见[模型适配指南](/tutorials/05-model-porting/model-porting)。

## 升级、恢复与证据边界

- 安装和 Web 升级共用 `cosmo-V<version>-<md5>.tar.gz` 生命周期；保持供电，恢复后同时
  核对服务状态和软件版本。
- 若服务未恢复，先检查 `systemctl status cosmo.service` 与服务日志；不要重复上传同一
  包掩盖首次失败。
- 完整目录、端口、持久化、失败恢复和回滚边界见[部署指南](/guide/deployment)。
- 历史公开容量结果见 [ScenarioBench v1.1](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/report.zh-CN.html)；短时最高点
  不是官方推荐配置，生产容量需按实际模型、视频和精度要求复验。
