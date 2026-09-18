# YOLO26-OBB 模型接入修改指南

> 目标：在 CosmoEdge（C++17 边缘 AI 引擎 + Vue3 前端）中新增 **yolo26-obb（旋转框检测）** 模型类型支持。
> 本文档基于对仓库的完整源码分析，给出从模型适配、后端集成到前端界面改造的**逐文件修改步骤与解释**。
> 配套文档：[CODE_WIKI.md](./CODE_WIKI.md)

---

## 目录

1. [总体方案](#1-总体方案)
2. [模型侧准备（权重导出约定）](#2-模型侧准备权重导出约定)
3. [后端修改（C++）](#3-后端修改c)
   - 3.1 [新增解码节点 `YoloObbDecodeNode`](#31-新增解码节点-yoloobbdecodenode)
   - 3.2 [节点类型注册（枚举/字符串映射/工厂）](#32-节点类型注册枚举字符串映射工厂)
   - 3.3 [后处理 Op 工厂 `MakeYoloObbPostOp`](#33-后处理-op-工厂-makeyoloobbpostop)
   - 3.4 [新增 `Yolo26ObbPipeline` 并注册](#34-新增-yolo26obbpipeline-并注册)
   - 3.5 [结果解析支持角度列](#35-结果解析支持角度列)
   - 3.6 [业务层透传角度（infer/flow）](#36-业务层透传角度inferflow)
   - 3.7 [模型服务层适配（导入校验/类别推断）](#37-模型服务层适配导入校验类别推断)
   - 3.8 [模型模板文件（各平台）](#38-模型模板文件各平台)
   - 3.9 [可视化画框支持旋转框（可选）](#39-可视化画框支持旋转框可选)
4. [前端修改（Vue3）](#4-前端修改vue3)
   - 4.1 [模型类型注册](#41-模型类型注册)
   - 4.2 [参数表单 schema](#42-参数表单-schema)
   - 4.3 [国际化文案](#43-国际化文案)
   - 4.4 [检测框绘制支持旋转框](#44-检测框绘制支持旋转框)
5. [修改文件清单汇总](#5-修改文件清单汇总)
6. [验证与测试步骤](#6-验证与测试步骤)
7. [风险与注意事项](#7-风险与注意事项)
8. [BM1688（Sophon）平台支持检查结论](#8-bm1688sophon平台支持检查结论)

---

## 1. 总体方案

### 1.1 现有 yolo26_det 的推理链路（参照物）

```text
config.json(model_type="yolo26_det")
  → DefaultComponent → ModelPipelineRegistry::Create("yolo26_det")
  → Yolo26DetPipeline::Init
      预处理: Resize + Normalize (MakeDetPreprocess)
      后处理: MakeYoloE2EPostOp → "yolo_e2e_postprocess" op
  → Graph: InputNode → Resize → Normalize → NetNode(模型推理) → YoloE2EDecodeNode
  → YoloE2EDecodeNode: 输入 [1, N, 6](x1,y1,x2,y2,score,cls) → 输出 [1, top_k, 6](cx,cy,w,h,score,cls)
  → ParseDetectionOutput → NetUtils::PickDetectionObjects → ObjectInfoV1
  → AiDetectorUnify → AiDetectRstEl(正框 box) → AiDetector → 告警/事件
```

### 1.2 OBB 的差异点

YOLO26-OBB 端到端导出后，每个检测框比普通检测**多一个角度分量**：

| 项 | yolo26_det | yolo26_obb_det |
| --- | --- | --- |
| 模型输出 | `[1, 300, 6]`：x1,y1,x2,y2,score,class | `[1, 300, 7]`：x1,y1,x2,y2,score,class,**angle** |
| 解码节点 | `YoloE2EDecodeNode`（top_col=6） | 新增 `YoloObbDecodeNode`（top_col=7） |
| 结果结构 | `ObjectInfoV1`（`angle` 字段已有但恒为 0） | 复用 `ObjectInfoV1`，填充 `angle` |
| 业务结构 | `AiDetectRstEl.box`（正框） | 增加 `angle` 字段，框仍用外接正框表达 |
| 前端绘制 | `strokeRect` 正矩形 | canvas 旋转矩形 |

**设计决策**（推荐方案）：

1. **新增独立模型类型 `yolo26_obb_det`**，不复用 `yolo26_det`——避免在一条链路里分支判断输出列数，且前端类型分组、模板、参数表单都按类型字符串组织，独立类型最干净。
2. **新增独立解码节点**而不是改 `YoloE2EDecodeNode`——`top_col` 从 6 变 7 会影响所有现有检测模型的张量布局，独立节点零回归风险。
3. **业务层用"外接正框 + 角度"表达**——跟踪、区域判定、绊线等下游逻辑全部基于 `util::Box` 正框，改动最小；角度作为附加字段透传给事件上报与前端绘制。
4. `ObjectInfoV1` **已预留 `angle` 字段**（`src/nn/utils/net_utils.h` L55），无需改结构体。

### 1.3 工作量分布

| 层 | 内容 | 文件数 |
| --- | --- | --- |
| 模型适配（nn） | 解码节点、节点注册、Op 工厂、Pipeline、结果解析 | ~10 |
| 系统集成（infer/flow/service） | 角度透传、导入校验、类别推断 | ~4 |
| 资源配置 | 各平台模型模板 + i18n | ~8 |
| 前端 | 类型注册、参数表单、画框、文案 | ~5 |

---

## 2. 模型侧准备（权重导出约定）

在改代码前，先确定模型导出契约（这决定后端解码逻辑）：

1. 使用 Ultralytics 训练 YOLO26-OBB（`yolo26n-obb.pt` 等）；
2. 导出**端到端**（带 NMS/TopK 后处理，`end2end=True`）模型，与现有 yolo26_det 保持一致风格：
   - ONNX（x86）：输出张量名自定（如 `output0`），形状 `[1, 300, 7]`，每行 **`(cx, cy, w, h, score, class_id, angle)`**——这是 Ultralytics end2end OBB 导出的**实际列序**（中心点 + 宽高，坐标为网络输入尺度像素值；0-1 归一化坐标也能被解码节点自动检测并反归一化）；`angle` 为弧度；
   - ONNX metadata 自带 `names` 键（如 `{0: 'z', 1: 't'}`），导入服务会自动读取并生成类别列表（见 §3.7），**无需在模板中手写 labels**；
   - Sophon `.nn`（BM1688/CV186X）：用 TPU-MLIR 转换，保持同样的输出契约；
   - Rockchip `.rknn`：注意当前 RKNN 后端白名单只支持 `yolov8_det/classify`（`ModelAddModel.cc` L186），RK3576 上支持 OBB 需要额外验证（见 §3.7）。
3. 若你的导出工具输出的是 `(x1, y1, x2, y2)` 角点形式而非 `(cx, cy, w, h)`，解码节点需相应调整（本文按 Ultralytics 实际的 cxcywh+angle 编写）。

> 关键原则：**把角度作为输出张量的第 7 列**，这样预处理、阈值过滤、标签体系与现有检测完全一致，改动集中在解码与结果解析。
>
> ⚠️ **列序是首要契约**：接入任何新 OBB 导出时，先用 onnxruntime 跑一张真实图，打印高分行的 7 列数值，确认列序与坐标尺度（中心/角点、像素/归一化）后再定解码逻辑。列序假设错误会导致框位置全部错乱且无明显报错。

---

## 3. 后端修改（C++）

### 3.1 新增解码节点 `YoloObbDecodeNode`

仿照 `src/nn/node/yolo_e2e_decode_node.h/.cc` 创建两个新文件。

**新建 `src/nn/node/yolo_obb_decode_node.h`**：

```cpp
#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

    // OBB 端到端解码节点：
    // 输入 [batch, N, 7] (cx, cy, w, h, score, class_id, angle)  ← Ultralytics end2end 实际列序
    // 输出 [batch, top_k, 7] (cx, cy, w, h, score, class_id, angle)
    class YoloObbDecodeNode : public Node {
    public:
        YoloObbDecodeNode();
        ~YoloObbDecodeNode() override;

        void LoadParam(Op* op) override;
        Status InferTopShapes() override;
        size_t GetBottomCount() override;
        size_t GetTopCount() override;
        Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                       std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    private:
        void ResetTopBlob(std::shared_ptr<Blob> top_blob);

        int top_k       = 300;
        float base_conf = 0.25f;
        int top_col     = 7;   // ★ 比 E2E 节点多 1 列角度
        int input_width_  = 0;
        int input_height_ = 0;
    };

}  // namespace cosmo::nn
```

**新建 `src/nn/node/yolo_obb_decode_node.cc`**（以 `yolo_e2e_decode_node.cc` 为模板，差异处已标注）：

```cpp
#include "nn/node/yolo_obb_decode_node.h"

#include <algorithm>
#include <cmath>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "util/Log.h"

namespace cosmo::nn {

    YoloObbDecodeNode::YoloObbDecodeNode() : Node() {
        node_type     = NodeType::NODE_YOLO_OBB_DECODE;   // ★ 新枚举值
        name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_OBB_DECODE).append("_0");
        one_blob_only = true;
    }

    YoloObbDecodeNode::~YoloObbDecodeNode() {}

    void YoloObbDecodeNode::LoadParam(Op* op) {
        if (!op) return;
        auto* post    = dynamic_cast<YoloPost*>(op);   // 复用 YoloPost op 承载参数
        top_k         = post->top_k;
        base_conf     = post->nms_detection_conf;
        input_width_  = post->input_width;
        input_height_ = post->input_height;
    }

    Status YoloObbDecodeNode::InferTopShapes() {
        top_blob_shapes     = {{max_batch, top_k, top_col}};   // [b, top_k, 7]
        top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
        return COSMO_NN_OK;
    }

    size_t YoloObbDecodeNode::GetBottomCount() { return 1; }
    size_t YoloObbDecodeNode::GetTopCount() { return 1; }

    Status YoloObbDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                      std::vector<std::shared_ptr<Blob>>& top_blobs) {
        timer.Start();
        auto bottom_blob = bottom_blobs.at(0);
        auto top_blob    = top_blobs.at(0);
        ResetTopBlob(top_blob);
        RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

        auto bottom_desc   = bottom_blob->GetBlobDesc();
        auto bottom_handle = bottom_blob->GetHandle();
        auto bottom_dim    = bottom_desc.dims;

        int batch   = bottom_dim.at(0);
        int box_num = bottom_dim.at(1);
        int box_col = bottom_dim.at(2);   // 应为 7
        if (box_col < 7)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "OBB output requires at least 7 columns");
        if (batch > static_cast<int>(max_batch))
            return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

        SetCurrentBatch(top_blob, batch);
        auto top_dim    = top_blob->GetBlobDesc().dims;
        auto top_handle = top_blob->GetHandle();
        int top_row     = top_dim.at(1);

        float* bottom_ptr = reinterpret_cast<float*>(bottom_handle.base);
        float* top_ptr    = reinterpret_cast<float*>(top_handle.base);

        for (int b = 0; b < batch; b++) {
            float* src = bottom_ptr + b * box_num * box_col;
            float* dst = top_ptr + b * top_row * top_col;

            // 归一化坐标自动检测：若采样的高分行 4 个框列都在 [0,1]，视为归一化
            bool is_normalized = false;
            if (input_width_ > 0 && input_height_ > 0) {
                is_normalized   = true;
                int check_count = std::min(20, box_num);
                for (int i = 0; i < check_count && is_normalized; i++) {
                    float sc = src[i * box_col + 4];
                    if (std::isnan(sc) || sc < base_conf) continue;
                    if (src[i * box_col + 0] > 1.0f || src[i * box_col + 1] > 1.0f ||
                        src[i * box_col + 2] > 1.0f || src[i * box_col + 3] > 1.0f)
                        is_normalized = false;
                }
            }

            int valid_count = 0;
            for (int i = 0; i < box_num && valid_count < top_k; i++) {
                float cx       = src[i * box_col + 0];   // ★ 模型直接输出中心点
                float cy       = src[i * box_col + 1];
                float w        = src[i * box_col + 2];   // ★ 模型直接输出宽高
                float h        = src[i * box_col + 3];
                float score    = src[i * box_col + 4];
                float class_id = src[i * box_col + 5];
                float angle    = src[i * box_col + 6];   // ★ 读取角度

                if (std::isnan(score) || score < base_conf)
                    continue;
                if (w <= 0.0f || h <= 0.0f)
                    continue;

                if (is_normalized) {
                    cx *= input_width_;  cy *= input_height_;
                    w  *= input_width_;  h  *= input_height_;
                }

                // (cx, cy, w, h) 直通（角度不随坐标缩放变化，直接透传）
                dst[valid_count * top_col + 0] = cx;
                dst[valid_count * top_col + 1] = cy;
                dst[valid_count * top_col + 2] = w;
                dst[valid_count * top_col + 3] = h;
                dst[valid_count * top_col + 4] = score;
                dst[valid_count * top_col + 5] = class_id;
                dst[valid_count * top_col + 6] = angle;   // ★ 写出角度
                valid_count++;
            }
        }
        timer.Stop();
        return COSMO_NN_OK;
    }

    void YoloObbDecodeNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
        // 与 YoloE2EDecodeNode::ResetTopBlob 相同实现
        auto top_desc = top_blob->GetBlobDesc();
        auto top_dim  = top_desc.dims;
        top_dim.at(0) = max_batch;
        top_desc.dims = top_dim;
        top_blob->SetBlobDesc(top_desc);
        int count   = DimsVectorUtils::Count(top_dim);
        auto handle = top_blob->GetHandle();
        float* data = static_cast<float*>(handle.base);
        std::fill(data, data + count, 0);
    }

}  // namespace cosmo::nn
```

> **解释**：解码节点是 Graph 中挂在模型输出之后的 host 算子。E2E 节点的职责是"过滤低分框 + 坐标反归一化 + 格式转换"，OBB 节点复用这套逻辑并把行宽从 6 扩到 7 透传角度。Ultralytics end2end OBB 导出的前 4 列**已经是** `(cx, cy, w, h)` 中心格式，因此解码节点直接透传、无需 xyxy→cxcywh 换算（早期版本按 xyxy 假设编写，实测框位置全部错乱，已修正）。角度在坐标缩放/平移下不变，因此 `AdjustSize` 映射回原图时无需变换角度。

**别忘了把新文件加入 `src/nn` 的 CMakeLists 源列表**（查看 `src/nn/CMakeLists.txt` 中 `yolo_e2e_decode_node.cc` 所在位置，照样添加）。

### 3.2 节点类型注册（枚举/字符串映射/工厂）

三处注册点，缺一会报 `Unknown node` 或创建出 `nullptr`：

**① `src/nn/node/node_type.h`** —— 在 `NODE_YOLO_E2E_DECODE` 后追加枚举：

```cpp
    NODE_YOLO_E2E_DECODE,
    NODE_YOLO_OBB_DECODE,    // ★ 新增
    NODE_SPLIT,
```

**② `src/nn/node/node_type_utils.cc`** —— 双向字符串映射：

`NodeTypeFromStr`（约 L43-L44 处）追加：

```cpp
    if (name == "yolo_e2e_postprocess")
        return NODE_YOLO_E2E_DECODE;
    if (name == "yolo_obb_postprocess")        // ★ 新增
        return NODE_YOLO_OBB_DECODE;
```

`NodeTypeToStr`（约 L96-L97 处）追加：

```cpp
        case NODE_YOLO_E2E_DECODE:
            return "yolo_e2e_decode";
        case NODE_YOLO_OBB_DECODE:             // ★ 新增
            return "yolo_obb_decode";
```

**③ `src/nn/node/host_node_creator.cc`** —— 节点工厂：

```cpp
        case NODE_YOLO_E2E_DECODE:
            return std::make_unique<YoloE2EDecodeNode>();
        case NODE_YOLO_OBB_DECODE:                       // ★ 新增
            return std::make_unique<YoloObbDecodeNode>();
```

并在该文件头部 `#include "nn/node/yolo_obb_decode_node.h"`（按现有 include 组织方式，可能集中在 `host_node.h` 中，则改在 `host_node.h` 添加）。

> **解释**：Pipeline 的 `Init` 只声明"输出节点挂一个名为 `yolo_obb_postprocess` 的 Op"，`Graph` 构建时通过 `NodeTypeFromStr(op名) → NodeType → NodeCreator::CreateNode` 实例化具体节点。这条链就是新模型类型的"接线板"。

### 3.3 后处理 Op 工厂 `MakeYoloObbPostOp`

**`src/nn/pipeline/pipeline_utils.h`**（L48 附近）声明：

```cpp
    std::unique_ptr<YoloPost> MakeYoloObbPostOp(float conf_threshold, int top_k, int input_width = 0,
                                                int input_height = 0);
```

**`src/nn/pipeline/pipeline_utils.cc`**（`MakeYoloE2EPostOp` 之后，约 L354）实现：

```cpp
    std::unique_ptr<YoloPost> MakeYoloObbPostOp(float conf_threshold, int top_k, int input_width,
                                                int input_height) {
        auto op                = std::make_unique<YoloPost>("yolo_obb_postprocess");  // ★ op 名
        op->nms_threshold      = 0;               // E2E 模型内部已做 NMS
        op->nms_detection_conf = conf_threshold;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }
```

> **解释**：复用现有 `YoloPost` op 结构（它只是参数载体），仅用新的 op 名 `"yolo_obb_postprocess"` 让组网时路由到新解码节点。

### 3.4 新增 `Yolo26ObbPipeline` 并注册

**`src/nn/pipeline/detection_pipeline.h`** —— 在 `Yolo26DetPipeline` 之后声明：

```cpp
    class PUBLIC Yolo26ObbPipeline : public ModelPipeline {
    public:
        Status Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                    int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                    const std::string& word_table_path, bool use_skip) override;
        Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) override;
        int GetMaxBatchSize() const override { return max_batch_; }
        std::string GetModelType() const override { return "yolo26_obb_det"; }
        OutputCategory GetOutputCategory() const override { return OutputCategory::DETECTION; }
        Status ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) override;

    private:
        int max_batch_ = 1;
    };
```

**`src/nn/pipeline/detection_pipeline.cc`** —— 实现（与 `Yolo26DetPipeline::Init` 的差异已标注）：

```cpp
// ============================= YOLO26 OBB (End-to-End) ====================

Status Yolo26ObbPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path,
                               bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "yolo26_obb_det";                    // ★

    for (auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);
        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = MakeDetPreprocess(p);              // 预处理与检测完全一致
            model.input_node_infos.push_back(std::move(input));
        }

        float conf_thresh = pipeline_utils::ReadFloat(p, "confidence_threshold", 0.25f);
        int top_k         = pipeline_utils::ReadInt(p, "top_k", 300);

        // OBB default input resolution is 1024x1024 (see model_template/yolo26_obb_det.json)
        int obb_input_w = 1024, obb_input_h = 1024;
        std::vector<int> input_size = pipeline_utils::ReadIntArray(p, "input_size", {}, 2);
        if (input_size.size() >= 2) {
            obb_input_w = input_size[0];
            obb_input_h = input_size[1];
        }

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            if (i == 0)
                output.op = pipeline_utils::MakeYoloObbPostOp(conf_thresh, top_k, obb_input_w,
                                                              obb_input_h);   // ★ OBB 后处理
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output";
        DimsVector out_shape = {-1, -1, 7};          // ★ 7 列
        if (!last.output_node_infos.empty())
            out_name = last.output_node_infos.front().name;
        BuildLabels(config, out_name, out_shape, model_info_);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status Yolo26ObbPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return DetectionForward(
        this, inputs, image_sizes_,
        [this](std::initializer_list<std::vector<std::shared_ptr<Blob>>> inp) { return RunGraph(inp); });
}

Status Yolo26ObbPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return DetectionParseOutput(GetGraphOutput(), image_sizes_, net_input_size_, selected_indices_,
                                selected_thresholds_, selected_classnames_, outputs);
}
```

文件末尾注册区（L437-L443）追加：

```cpp
REGISTER_MODEL_PIPELINE("yolo26_det", Yolo26DetPipeline);
REGISTER_MODEL_PIPELINE("yolo26_obb_det", Yolo26ObbPipeline);   // ★ 新增
```

> **解释**：`REGISTER_MODEL_PIPELINE` 宏在静态初始化期把 `"yolo26_obb_det"` 写入 `ModelPipelineRegistry`。之后 `DefaultComponent` 读到 `config.json` 中 `model_type="yolo26_obb_det"` 即可自动创建该 Pipeline——这是新模型类型接入推理引擎的**唯一必选注册点**。

### 3.5 结果解析支持角度列

`ParseDetectionOutput → NetUtils::PickDetectionObjects`（`src/nn/utils/net_utils.cc` L195-L286）当前硬编码读 6 列。改为**按列数自适应**，对 6 列模型零影响：

```cpp
        for (int i = 0; i < row; i++) {
            float x      = b_data[i * col + 0];
            float y      = b_data[i * col + 1];
            float w      = b_data[i * col + 2];
            float h      = b_data[i * col + 3];
            float c      = b_data[i * col + 4];
            int class_id = static_cast<int>(b_data[i * col + 5]);
            float angle  = (col >= 7) ? b_data[i * col + 6] : 0.f;   // ★ 第 7 列为角度
            ...
            ObjectInfoV1 obj_info;
            obj_info.x1 = x1;  obj_info.y1 = y1;
            obj_info.x2 = x2;  obj_info.y2 = y2;
            obj_info.angle = angle;                                    // ★ 填充角度
            ...
        }
```

> **解释**：
> - `x1..y2` 仍是旋转框的**外接正框**（由解码节点从模型输出的 cxcywh 换算得来）。对精确的旋转框四角点，若业务需要（如精确区域判定），可在解码节点中额外输出四角点，但第一期不建议——下游全部按正框消费。
> - `AdjustSize`（坐标映射回原图）只缩放平移坐标，角度保持不变，无需修改。

### 3.6 业务层透传角度（infer/flow）

**① `src/infer/AiCommon.h`** —— `AiDetectRstEl` 增加角度字段（L26-L56 结构体内）：

```cpp
struct AiDetectRstEl {
    util::Box box;
    float angle{0.0f};      // ★ OBB 旋转角度（弧度），普通检测恒为 0
    ...
};
```

**② `src/infer/AiDetectorUnify.cc`** —— `Forward` 结果转换处（L191-L211）：

```cpp
            AiDetectRstEl el;
            el.box.x      = static_cast<int>(obj.x1);
            el.box.y      = static_cast<int>(obj.y1);
            el.box.width  = static_cast<int>(obj.x2) - static_cast<int>(obj.x1) + 1;
            el.box.height = static_cast<int>(obj.y2) - static_cast<int>(obj.y1) + 1;
            el.angle      = obj.angle;          // ★ 透传角度
            ...
```

**③ 事件上报（可选但推荐）**：告警/事件序列化处（`src/flow/alarm/` 与事件 DTO，搜索现有输出 `box`/`hwRatio` 字段的位置）把 `angle` 加入事件 JSON，例如：

```json
{ "box": {"x":..,"y":..,"width":..,"height":..}, "angle": 0.52, ... }
```

这样前端图片分析、事件回放才能拿到角度画旋转框。

**④ 跟踪与区域逻辑**：`AiTracker`、区域/绊线判定（`AiDetector::TargetAddArea/TargetAddLine`）继续使用 `box` 正框，**无需改动**。若未来要精确旋转框 IoU 跟踪，再单独扩展。

> **解释**：业务层对"框"的消费面非常广（跟踪、区域、绊线、计数、OSD、告警图），用外接正框保证这些逻辑全部免改；角度作为"附加渲染信息"沿 `ObjectInfoV1.angle → AiDetectRstEl.angle → 事件 JSON → 前端` 单线透传，是风险最小的集成路径。

### 3.7 模型服务层适配（导入校验/类别推断）

**① `src/service/model/impl/ModelAddModel_Json.cc`** —— 类别数推断（L52-L56）：

OBB 输出每行 7 列 = 4 框 + 1 分数 + 1 角度 + N 类，但端到端模型输出已是 `[1, top_k, 7]` 固定形状，类别数不从形状推断，直接给 OBB 类型一个分支：

```cpp
    const auto class_field_count = [&]() {
        if (modelType == "yolov5_det" || modelType == "yolo26_det")
            return 5;
        if (modelType == "yolo26_obb_det")
            return 6;                       // ★ 4框+1分数+1角度
        return 4;
    };
```

> 说明：`InferClassCountFromShape` 对 `[1,300,7]` 会算出 `7-6=1`，这不符合实际类别数。端到端输出 `[1, top_k, 6/7]` 不承载类别数信息，直接返回 0 跳过形状推断：

```cpp
    int InferClassCountFromShape(const std::string& modelType, const std::vector<int>& shape) {
        if (shape.empty()) return 0;
        // 端到端输出 [1, top_k, 6/7] 不承载类别数信息
        if ((modelType == "yolo26_det" || modelType == "yolo26_obb_det") && shape.size() == 3 &&
            (shape[2] == 6 || shape[2] == 7))
            return 0;
        ...
    }
```

**①-b 类别自动提取（ONNX metadata `names`）**：端到端 OBB 的形状推不出类别数，但 Ultralytics 导出的 ONNX 在 metadata 中自带 `names` 键（如 `{0: 'z', 1: 't'}`）。导入时自动读取并生成 labels，避免"只显示一个类别"：

**`src/infer/BmodelTool.h`** —— `BmodelInfo` 增加字段：

```cpp
struct BmodelInfo {
    ...
    // ONNX metadata "names" 原始串，如 "{0: 'z', 1: 't'}"；无则为空
    std::string class_names_raw;
};
```

**`src/infer/BmodelTool.cc`** —— x86/ONNX 分支读取 metadata：

```cpp
    Ort::ModelMetadata metadata = session.GetModelMetadata();
    auto namesPtr = metadata.LookupCustomMetadataMapAllocated("names", allocator);
    if (namesPtr)
        info.class_names_raw = namesPtr.get();
```

**`src/service/model/impl/ModelAddModel_Json.cc`** —— 解析并填充（`UpdateTemplateConfig` 末尾）：

```cpp
    // 正则提取 "数字: '名称'" 对，按 id 排序后生成 labels
    std::vector<std::pair<int, std::string>> ParseOnnxClassNames(const std::string& raw);
    bool FillLabelsFromOnnxMetadata(nlohmann::json& doc, const std::string& modelType,
                                    const std::vector<cosmo::BmodelInfo>& bmodel_infos);

    if (!FillLabelsFromOnnxMetadata(templateDoc, modelType, bmodel_infos)) {
        FillDefaultLabels(templateDoc, modelType);   // 无 metadata 时回退原逻辑
    }
```

> 说明：metadata 缺失时（非 Ultralytics 导出）回退到原有形状推断/模板 labels 流程，行为不变；导入后仍可在模型配置页（ParamsConfig 类别编辑）人工修正类别名。Sophon/RKNN 后端无 metadata 概念，BM1688/CV186x 导入沿用模板 labels 或人工配置。

**② `src/service/model/impl/ModelServiceCrud.cc`** —— 输出格式校验（L98-L128）：

在现有 `yolov8/9/11/12` 分支后追加：

```cpp
    } else if (model_type == "yolo26_obb_det") {
        // 端到端 OBB：单输出，形状 [1, top_k, 7]
        bool reject = (outputs.size() != 1);
        if (!reject) {
            std::vector<int> shape = get_shape(outputs[0]);
            reject = !(shape.size() == 3 && shape[2] == 7);
        }
        if (reject) {
            throw cosmo::util::ErrorMessage(
                cosmo::util::make_error_condition(cosmo::util::ErrorEnum::ParameterException),
                "当前模型输出格式与 yolo26_obb_det 旋转框检测模型不匹配（期望单输出 [1, top_k, 7]）。添加不成功");
        }
    }
```

**③ `src/service/model/impl/ModelAddModel.cc`** —— RKNN 白名单（L186，仅当要在 Rockchip 上支持时）：

```cpp
        std::vector<std::string> types{"yolov8_det", "classify"};
        // types.emplace_back("yolo26_obb_det");   // ★ 需先验证 RKNN 端到端 OBB 张量契约后再放开
```

> 说明：模板文件按 `<modelType>.json` 名读取（`ModelAddModel.cc` L599），只要模板文件存在即可，无需改读取逻辑。`IsDetectionModel` 按 `_det` 后缀判断——`yolo26_obb_det` **以 `_det` 结尾**，自动命中检测模型语义，默认阈值走 0.25 分支（`ReadDefaultThreshold`），无需额外修改：

```cpp
    double ReadDefaultThreshold(const std::string& modelType) {
        if (IsDetectionModel(modelType))
            return 0.25;
        return 0.5;
    }
```

> 命名决策：OBB 类型最终采用 `yolo26_obb_det`（而非最初的 `yolo26_obb`），以自动命中 `_det` 后缀约定——默认阈值、统计口径等所有按 `IsDetectionModel` 判定的位置都无需显式补充。

### 3.8 模型模板文件（各平台）

在**每个平台资源目录**新增 `yolo26_obb_det.json`：

- `data/resource/aiboxresource_x86/model_template/yolo26_obb_det.json`
- `data/resource/aiboxresource_bm1688/model_template/yolo26_obb_det.json`
- `data/resource/aiboxresource_cv186x/model_template/yolo26_obb_det.json`
- （RKNN 平台视 §3.7 决定）

内容（以 x86 为例，`chip_type` 按平台改为 `BM1688`/`CV186X`）：

```json
{
  "model_type": "yolo26_obb_det",
  "chip_type": "X86",
  "algorithm_code": "4000001",
  "version": "V1.0.0",
  "reduce": "",
  "models": [
    {
      "name": "yolo26旋转框检测",
      "file_name": "",
      "file_md5": "",
      "max_batch": 1,
      "inputs": [
        { "name": "images", "shape": [1, 3, 1024, 1024], "data_type": 0 }
      ],
      "outputs": [
        { "name": "output0", "shape": [1, 300, 7], "data_type": 0 }
      ],
      "params": {
        "input_size": [1024, 1024],
        "padding_color": [114, 114, 114],
        "normalize_mean": [0, 0, 0],
        "normalize_scale": 0.00392157,
        "is_bgr": false,
        "confidence_threshold": 0.25,
        "top_k": 300,
        "gravity": 1
      }
    }
  ],
  "labels": [
    { "id": "0", "name": "object", "threshold": [0.25, 0.25] }
  ]
}
```

> **解释**：模板是"新增模型"向导的骨架——用户上传权重后，系统用模板 + 实际模型文件的张量信息合成该模型目录里的 `config.json`。`outputs.shape` 的最后一维 **必须是 7**，与解码节点契约一致。

同时在各平台 `i18n/resource.zh-CN.json` 与 `resource.en-US.json` 添加模型名文案（参照现有键格式）：

```json
"resource.model.4000001.yolo26_obb_det.name": "yolo26旋转框检测",
```

### 3.9 可视化画框支持旋转框（图片分析标注图已实现）

**实测教训**：图片分析页预览底图用的是后端烘焙的 `fullPicture`（`index.vue` L247 `preview: res.fullPicture`），前端 canvas 叠加层虽有 `ctx.rotate(angle)`，但用户看到的红框来自后端标注图。**若后端只画正矩形，标注图上的框不会按角度旋转**，目视即"框没有按 angle 旋转"。

**实现**（已落地）：

① `src/util/GeometricPos.h/.cc` 新增 `GetRotatedBoxOsdLines(box, angle_rad)`：绕框中心旋转四角，返回四条边线段。旋转矩阵与 HTML canvas `ctx.rotate()` 完全一致（图像坐标 y 向下，**正角=屏幕顺时针**）：

```cpp
// x' = cx + dx*cos(a) - dy*sin(a)
// y' = cy + dx*sin(a) + dy*cos(a)
```

② `src/flow/task/PTaskBaseUpload.cc` `DetTargetHandFullPicture` 画框处按角度分支：

```cpp
auto boxLines = (std::fabs(target.angle) > 1e-6f)
                    ? GetRotatedBoxOsdLines(box, target.angle)
                    : GetBoxOsdLines(box, origImg->GetWidth(), origImg->GetHeight());
```

> 注意：CPU 画线（`VideoFrameProcCpu::DrawLines`）逐像素越界检查，旋转后超出图像边界的线段自动裁剪，无需手动 clamp；Sophon 后端（`VideoFrameDrawing.cc`）会**丢弃**任一端点越界的线段，若 BM1688/CV186X 也要旋转框需先对线段做裁剪（详见 §8.2 ② 及修复方向）。
>
> 告警抓图（`TaskAlarmPicture.cc`）与实时预览叠加（`StreamViewerLiveData.cc`）仍画正框，如需旋转框可复用 `GetRotatedBoxOsdLines`。

---

## 4. 前端修改（Vue3）

### 4.1 模型类型注册

**`src/web/src/views/gam/countManagement/atomicModel/index.vue`**：

① `subTypeToMain` 映射（L343-L348）：

```js
const subTypeToMain = {
  yolov5_det: 'detect', yolov8_det: 'detect', yolov9_det: 'detect',
  yolov11_det: 'detect', yolov12_det: 'detect', yolo26_det: 'detect',
  yolo26_obb_det: 'detect',                                    // ★ 新增
  classify: 'classify', keypoints: 'keypoints', feature: 'feature', ocr: 'ocr',
  dino: 'foundation', sam2: 'foundation', qwen3vl: 'foundation', qwen3_5: 'foundation'
}
```

> 效果：`loadModelTypes()` 会自动对 `yolo26_obb_det` 调 `atomicModelList` 拉取该类型模型，类型 Tab 计数与筛选自动生效。

② `modelTypeGroups` 下拉分组（L575-L626）detect 组 children 追加：

```js
      { label: 'yolo26_det', value: 'yolo26_det' },
      { label: 'yolo26_obb_det', value: 'yolo26_obb_det' }        // ★ 新增
```

> 注意 L620-L625 的 RKNN 过滤逻辑：`supported = new Set(['yolov8_det', 'classify'])`。若 Rockchip 平台也要暴露 OBB，把 `'yolo26_obb_det'` 加入 `supported`（前提是后端 §3.7 ③ 已放开）。

### 4.2 参数表单 schema

**`src/web/src/views/gam/countManagement/modelConfig/ParamsConfig.vue`** `TYPE_SCHEMA`（L346-L401）：

```js
    yolo26_det: {
      common: ['input_size', 'gravity', 'confidence_threshold', 'top_k'],
      advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
    },
    yolo26_obb_det: {                                          // ★ 新增（与 yolo26_det 一致）
      common: ['input_size', 'gravity', 'confidence_threshold', 'top_k'],
      advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
    },
```

### 4.3 国际化文案

**`src/web/public/resource-i18n/resource.zh-CN.json` 与 `resource.en-US.json`**：

现有 `yolo26_det` 文案来自资源包键（`resource.model.4000001.yolo26_det.name`，已在 §3.8 处理）。若前端还有本地词汇表（`glossary.*`）引用模型类型显示名，按现有 `detectAlg` 词汇检查是否需要补充"旋转框检测"说明文案。

### 4.4 检测框绘制支持旋转框

**`src/web/src/views/gam/imageAnalysis/index.vue`** `drawOverlay`（L446-L493）：

当前逻辑：

```js
const { x, y, width: bw, height: bh } = target.box
ctx.strokeRect(x, y, bw, bh)
```

改为支持角度（后端事件/分析结果需已携带 `angle`，见 §3.6 ③）：

```js
const { x, y, width: bw, height: bh } = target.box
const angle = target.angle || 0
if (bw > 0 || bh > 0) {
  ctx.strokeStyle = color
  ctx.lineWidth = 2
  if (angle) {
    // 绕框中心旋转绘制
    const cx = x + bw / 2
    const cy = y + bh / 2
    ctx.save()
    ctx.translate(cx, cy)
    ctx.rotate(angle)          // 弧度
    ctx.strokeRect(-bw / 2, -bh / 2, bw, bh)
    ctx.restore()
  } else {
    ctx.strokeRect(x, y, bw, bh)
  }
}
```

> 说明：
> - 标签文字锚点（`labelX/labelY`）用未旋转的框左上角即可，无需跟随旋转；
> - 若场景配置页/实时预览页也有独立画框组件（搜索 `strokeRect` 的其他出处），按同样模式扩展；
> - 实时视频预览的叠加框是**后端画在视频流上**的（§3.9），前端只负责图片分析类页面。

**同文件"检测结果详情"表格的字段名修复**：后端 `box` 为 `MsgRectReal`，JSON 字段是 `x/y/width/height`（**不是** `w/h`）。位置列必须读 `width/height`，否则显示为 0：

```html
<span v-if="row.box">
  [{{ (row.box.x || 0).toFixed(3) }}, {{ (row.box.y || 0).toFixed(3) }},
   {{ (row.box.width || 0).toFixed(3) }}, {{ (row.box.height || 0).toFixed(3) }}]
</span>
```

---

## 5. 修改文件清单汇总

### 后端新增文件

| 文件 | 内容 |
| --- | --- |
| `src/nn/node/yolo_obb_decode_node.h` | OBB 解码节点声明 |
| `src/nn/node/yolo_obb_decode_node.cc` | OBB 解码节点实现 |
| `data/resource/aiboxresource_x86/model_template/yolo26_obb_det.json` | x86 模板 |
| `data/resource/aiboxresource_bm1688/model_template/yolo26_obb_det.json` | BM1688 模板 |
| `data/resource/aiboxresource_cv186x/model_template/yolo26_obb_det.json` | CV186X 模板 |
| `data/resource/aiboxresource_x86/algorithm_template/38874_旋转框检测算法_*.json` | x86 开箱即用 OBB 算法模板 |
| `data/resource/aiboxresource_bm1688/algorithm_template/38874_旋转框检测算法_*.json` | BM1688 开箱即用 OBB 算法模板 |
| `data/resource/aiboxresource_cv186x/algorithm_template/38874_旋转框检测算法_*.json` | CV186X 开箱即用 OBB 算法模板 |

### 后端修改文件

| 文件 | 修改点 |
| --- | --- |
| `src/nn/node/node_type.h` | 枚举 `NODE_YOLO_OBB_DECODE` |
| `src/nn/node/node_type_utils.cc` | 字符串 ↔ 枚举双向映射 |
| `src/nn/node/host_node_creator.cc` | 工厂分支创建 `YoloObbDecodeNode` |
| `src/nn/pipeline/pipeline_utils.h/.cc` | `MakeYoloObbPostOp` |
| `src/nn/pipeline/detection_pipeline.h/.cc` | `Yolo26ObbPipeline` + `REGISTER_MODEL_PIPELINE("yolo26_obb_det", ...)` |
| `src/nn/utils/net_utils.cc` | `PickDetectionObjects` 按列数读取角度 |
| `src/nn/CMakeLists.txt` | 新节点源文件 |
| `src/infer/AiCommon.h` | `AiDetectRstEl.angle` |
| `src/infer/AiDetectorUnify.cc` | 透传 `obj.angle` |
| `src/service/model/impl/ModelAddModel_Json.cc` | 类别数推断/默认阈值 + ONNX metadata 类别自动填充 |
| `src/service/model/impl/ModelServiceCrud.cc` | OBB 输出格式校验 |
| `src/infer/BmodelTool.h/.cc` | `BmodelInfo.class_names_raw` + 读取 ONNX metadata `names` |
| `src/service/model/impl/ModelAddModel.cc` | （可选）RKNN 白名单 |
| `data/resource/*/i18n/resource.*.json` | 模型名文案 |
| `src/flow/alarm/TaskAlarmPicture.cc`、`src/flow/stream/StreamViewerLiveData.cc` | （可选）旋转框绘制 |

### 前端修改文件

| 文件 | 修改点 |
| --- | --- |
| `src/web/src/views/gam/countManagement/atomicModel/index.vue` | `subTypeToMain` + `modelTypeGroups` |
| `src/web/src/views/gam/countManagement/modelConfig/ParamsConfig.vue` | `TYPE_SCHEMA['yolo26_obb_det']` |
| `src/web/src/views/gam/imageAnalysis/index.vue` | `drawOverlay` 旋转框 + 位置列字段 `width/height` |
| `src/web/public/resource-i18n/resource.*.json` | （如需）文案 |

---

## 6. 验证与测试步骤

1. **编译验证**（x86 后端最快）：
   ```bash
   cmake -B build -DCOSMO_NN_USE_CPU_BACKEND=ON -DCOSMO_NN_USE_SOPHON_BACKEND=OFF -DBUILD_TESTS=ON
   cmake --build build -j
   ```
   关注点：新节点源文件已进 CMake、枚举与映射无遗漏（漏映射会在 `InitGraph` 时报 `Unknown node type`）。
2. **单元测试**：仿照 `test/` 中现有解码节点测试，为 `YoloObbDecodeNode` 构造 `[1, N, 7]` 输入，断言低分过滤、归一化反算、角度透传、`[1, top_k, 7]` 输出布局。
3. **模型导入验证**：
   - 启动服务（x86 docker compose），Web 控制台"原子模型 → 新增"，主类型选检测、子类型应出现 `yolo26_obb_det`；
   - 上传 OBB 的 `.onnx`，确认输出形状校验通过、模型目录生成、`config.json` 中 `model_type` 为 `yolo26_obb_det`；
   - 故意上传 `[1,300,6]` 输出的普通检测模型选 OBB 类型，应被 §3.7 ② 的校验拒绝。
4. **推理验证**：用"图片分析"页面上传含倾斜目标的测试图，确认返回结果含 `box` 与 `angle`，前端画出旋转框且角度方向正确（重点核对角度符号/基准轴）。
   - **响应结构（实测教训）**：`PTaskDetectPic` 的目标在 `resData.areaList[i].targetList` 中，**顶层没有 `targetList`**；脚本化验证时直接读顶层会得到空列表，误判为"无检测结果"。
   - **请求体限制**：HTTP body 上限 1MB（`httpBodyTooLarge`）。大图走前端分块上传（`uploadTemp` → `uploadId` → 检测）；脚本直传 `imageBase64` 时需先把图压到 ~700KB 以内。
5. **任务链路验证**：创建场景编排 → 绑定 OBB 模型 → 启动检测任务 → 确认事件/告警 JSON 携带角度，告警图正常（若做了 §3.9 则检查旋转框渲染）。
6. **回归验证**：导入一个现有 `yolo26_det` 模型跑同样流程，确认 6 列链路不受 `PickDetectionObjects` 改动影响（`col>=7` 分支不触发）。

## 7. 风险与注意事项

1. **角度语义一致性**：训练框架（Ultralytics OBB 的角度范围与方向定义）→ 模型导出 → 解码透传 → `cv::RotatedRect`/canvas 绘制，每一环的角度制（弧度/角度）、方向（顺/逆时针）、基准轴（宽边/长边）都可能不同，**联调时必须用已知角度的标定图逐环校验**。
2. **外接正框放大效应**：业务层用外接正框做区域/绊线判定时，大角度细长目标（如斜置车辆）的正框会明显大于实际目标，可能引入误报。若业务敏感，二期可在区域判定中使用旋转框-多边形相交。
3. **跟踪器**：现有跟踪基于正框 IoU，旋转框场景下目标密集时可能出现 ID 切换，属已知近似，先观察后优化。
4. **RKNN 平台**：端到端 OBB 在 RKNN 上的张量契约（量化输出、NPU 后处理）未经验证，默认不放开 RKNN 白名单，避免导入后运行期失败。
5. **`_det` 后缀约定**：系统多处用 `_det` 后缀识别检测模型（`IsDetectionModel`）。OBB 类型命名为 `yolo26_obb_det`，自动命中该约定，默认阈值、统计口径等位置无需显式补充。
6. **模板与权重绑定**：模板只是骨架，公开仓库不附带 OBB 权重；`algorithm_template/` 已提供开箱即用的 `38874_旋转框检测算法` 模板（三平台），其检测动作通过模型选择器绑定 `yolo26_obb_det` 原子模型（`atomicCode=4000001`），用户导入权重后即可直接编排使用。
7. **列序契约（实测教训）**：Ultralytics end2end OBB 输出为 `(cx, cy, w, h, score, class_id, angle)`。若按 `(x1,y1,x2,y2)` 解码，中心坐标被当作角点、宽高算出负值，表现为"框位置全部错乱、大量框被过滤"，且无报错。接入新导出务必先按 §2 的警告做列序验证。
8. **类别数来源（实测教训）**：端到端 `[1,300,7]` 推不出类别数，模板默认 1 类会导致"只显示一个类别"。x86 导入已改为自动读取 ONNX metadata `names`（§3.7 ①-b）；非 Ultralytics 导出或 Sophon/RKNN 平台需人工在模型配置页维护类别。

---

## 8. BM1688（Sophon）平台支持检查结论

> 结论：**代码架构上完全支持 BM1688，无需改动即可交叉编译运行**——编译、注册、节点创建、输出回传全链路平台无关。但存在两个平台行为差异（类别不自动填充、旋转框越界线段被丢弃），部署 BM1688 前需按下表逐项核对。

### 8.1 支持链路核对表（源码证据）

| 环节 | 证据 | 结论 |
| --- | --- | --- |
| 模板资源 | `data/resource/aiboxresource_bm1688/model_template/yolo26_obb_det.json` | BM1688 资源已就位（三平台齐备） |
| Pipeline 注册 | `src/nn/pipeline/detection_pipeline.cc` L522 `REGISTER_MODEL_PIPELINE("yolo26_obb_det", ...)` | 无平台条件；推理后端由 `device_type` 参数决定 |
| 解码节点编译 | `src/nn/CMakeLists.txt` L15 `GLOB node/*.cc` | 无条件编译，`yolo_obb_decode_node.cc` 会编进所有平台固件 |
| 节点创建回退 | `src/nn/node/node_type_utils.cc` L144-160 `CreateByType` | **关键机制**：sophon creator 不认识 `NODE_YOLO_OBB_DECODE` 时自动回退 `HostNodeCreator`（注册于 `host_node_creator.cc`，L20-21）创建 CPU 版解码节点。与 `yolo26_det` 的 `NODE_YOLO_E2E_DECODE` 同款机制（det 已在 BM1688 运行，OBB 同构） |
| 预处理 | `src/nn/device/sophon/sophon_node_creator.cc` L13-22 | `NODE_RESIZE`/`NODE_NORMALIZE` 由 Sophon creator 创建，走 BMCV 加速 |
| 推理输出回传 | `src/nn/device/sophon/sophon_net_node.cc` L503/L551 `CopyOutputToBlob` | `output_to_cpu_` 路径做 d2s 拷贝 + dtype 转换（bmodel 量化输出转 float），host 解码节点直接消费（`yolov8_decode` 同样用法，L503 注释明确该设计） |
| 后处理 Op | `src/nn/utils/op.cc` L83 | `YoloPost` 是纯 CPU host op，`MakeYoloObbPostOp` 平台无关 |
| 导入校验 | `src/service/model/impl/ModelServiceCrud.cc` L128-138 | `[1, top_k, 7]` 输出校验平台无关 |

### 8.2 平台差异（BM1688 专属）

**① 类别不会自动填充，需人工维护**

`FillLabelsFromOnnxMetadata` 依赖的 `class_names_raw` 只在 **ONNX 分支**读取（`src/infer/BmodelTool.cc` L418）；BM1688 导入 `.bmodel` 时 bmrt API 不携带 Ultralytics `names` metadata，而 `InferClassCountFromShape`（`ModelAddModel_Json.cc` L56-58）对 `[1,300,7]` 明确返回 0（端到端输出无类别数信息）→ **labels 为空，用户须在模型配置页手填类别**。这是 end2end 模型在非 x86 平台的共性限制（`yolo26_det` 同样如此），非 OBB 特有。

**② 旋转框 OSD 越界线段被整条丢弃**

x86 的 CPU `DrawLines`（`VideoFrameProcCpu.cc`）逐像素越界检查，线段画到图像边界为止；BM1688 的 BMCV 实现（`src/media/VideoFrameDrawing.cc` L91-99）**任一端点越界（负坐标或超出宽高）即 `continue` 跳过整条线段**。`GetRotatedBoxOsdLines`（§3.9）旋转后四角若超出图像（目标贴近画面边缘时常见），该边在 BM1688 标注图上会**整条消失**。

修复方向（若需 BM1688 对齐 x86 行为）：在 `PTaskBaseUpload.cc` 把线段送画前按图像边界做线段裁剪（如 Cohen–Sutherland），保证两条端点都落在图内；CPU 实现可保持现状（逐像素已天然裁剪）。

### 8.3 待实测项（本机无 BM1688 环境无法覆盖）

1. **TPU-MLIR 转换**：`.onnx` → `.bmodel` 时输出保持 `[1, 300, 7]`、dtype fp32（含 angle 列不丢）；量化模型需确认 angle 列的量化精度可接受。
2. **交叉编译**：BM1688 Sophon 交叉编译链路整体构建通过（`patch_srs_crossbuild.sh` 等补丁不涉及 OBB 代码，风险低但需跑通一次）。
3. **端到端验证**：导入 → 图片分析检测 → 标注图旋转框渲染（重点验证 §8.2 ② 边界目标）→ 任务链路告警图。

---

*本指南与 [CODE_WIKI.md](./CODE_WIKI.md) 配套使用：Wiki 提供架构全貌，本文提供逐文件落地步骤。*
