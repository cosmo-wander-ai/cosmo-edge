# YOLO26 OBB 接入说明

`yolo26_obb_det` 通过独立的模型适配层接入旋转框检测。模型的列序和角度单位在适配层解释；框架使用水平外接框和可选的四角坐标，不再让水平框同时表示旋转矩形的宽高。

本文描述接口约定、接入位置和验证方法。模板存在或代码编译通过，不代表某个设备、权重或转换产物已经通过验收；具体运行结果应记录在对应任务的验证报告中。

## 模型输入输出约定

当前适配器接受一个 float32 输出张量，形状为 `[batch, N, 7]`。每一行依次为：

```text
cx, cy, width, height, confidence, class_id, angle
```

- 前四列表示旋转矩形的中心和边长，不是水平外接框，也不是 `x1,y1,x2,y2`。
- `coordinate_units` 默认为 `pixels`，坐标相对于网络输入图像。对于明确导出为归一化坐标的模型，配置为 `normalized`；适配器不根据数值大小猜测单位。
- `angle_units` 仅支持 `radians`。角度基于图像坐标系，x 向右、y 向下；零角度也是合法的 OBB。
- `input_size` 顺序为 `[height, width]`，需要与实际输入张量匹配。非方形输入不能交换这两个值。
- 当前 Sophon OBB 接入要求输入宽度为 64 的倍数（如 640、1024）；672 这类宽度会在初始化时报错。现有 Sophon resize 会将可见宽度向上对齐，而 normalize 使用模型原宽度，因此在底层分离步长对齐和可见尺寸前不接受此配置。此限制不适用于 CPU 输入宽度。
- 模型应已经完成端到端候选筛选。不同的原始检测头、列序或角度约定需要对应的模型适配器。
- 导入时的张量形状检查无法证明列语义正确，仍需用已知输入与上游推理结果核对。

三个平台的模型模板位于 `data/resource/aiboxresource_{x86,bm1688,cv186x}/model_template/yolo26_obb_det.json`。默认参数包括：

```json
{
  "input_size": [1024, 1024],
  "gravity": 1,
  "coordinate_units": "pixels",
  "angle_units": "radians",
  "confidence_threshold": 0.25,
  "top_k": 300
}
```

`gravity` 沿用各后端现有预处理：`0` 拉伸、`1` 等比居中填充；`2` 在 CPU/RKNN 为左上填充，在 Sophon 为居中裁剪源图。居中填充的整数尺寸也存在 CPU 截断、Sophon 四舍五入的差异。`MakeObbResizeTransform` 按实际后端还原缩放、整数尺寸、填充或裁剪偏移，再求水平外接框。不得先裁剪中心宽高破坏旋转几何；非等比拉伸还原后保留完整四边形。

## 框架结果约定

```text
模型 [cx,cy,w,h,score,class,angle]
    → YoloObbDecodeNode：校验并转为网络输入坐标下的四角
    → Yolo26ObbPipeline：按预处理变换还原至原图
    → 通用检测结果：水平外接框 + 可选四角
    → 跟踪、业务处理、绘制和结果序列化
```

解码节点与 OBB pipeline 之间使用专用的内部表示：四个顶点的八个坐标，加上置信度和类别。普通检测解析器不以“存在第七列”猜测 OBB；模型原始输出也不需要随这个内部表示改变。

通用结果中的 `box` 保持原有水平框含义，供现有跟踪、裁图和区域判断使用。旋转几何单独保存，不能用“水平外接框 + angle”重建，也不能用非零角度判断是否存在旋转几何。区域业务仍按水平框判断；此接入不改变区域判断为旋转多边形相交。

API 中的可选字段为 `orientedCorners`，包含顺序连接的四个 `{x, y}` 浮点坐标，始终使用原图像素。图片分析和报警目标的 `box` 也使用原图像素；概览和行为记录中的 `box` 保留原有归一化坐标，像素框为 `aiBox`。普通检测结果不输出四角。例如图片分析目标：

```json
{
  "box": {"x": 40, "y": 10, "width": 51, "height": 51},
  "orientedCorners": [
    {"x": 50.25, "y": 10.5},
    {"x": 90.25, "y": 50.5},
    {"x": 80.25, "y": 60.5},
    {"x": 40.25, "y": 20.5}
  ]
}
```

报警录像元数据保留 `rects` 中原有的 `xRatio/yRatio/wRatio/hRatio`，每个目标可增加原图像素的 `orientedCorners`，帧记录增加 `sourceWidth/sourceHeight`。回放按视频实际显示尺寸缩放四角，并裁剪到视频显示区；旧录像以及无有效源图尺寸的记录继续显示原有 AABB。

四角保持测量几何；图像边界上的可见线段在绘制时裁剪。不得逐个钳制顶点再把所得多边形作为模型测量结果。

### 跟踪

现有跟踪器继续用水平框执行 Kalman 预测与关联，不引入旋转 IoU 或角度运动模型。输入检测携带本帧的来源索引，经过置信度分组和匹配后，适配层按该索引取回对应检测的 `targetId` 和旋转四角。不能通过水平框浮点坐标相等来查找原检测。

本帧没有匹配检测的轨迹，只输出预测水平框，不沿用上一帧四角作为本帧测量。目标重新匹配后，四角来自当前帧检测。若业务需要旋转轨迹预测，应另外扩展跟踪模型。

### 绘制与前端

后端以统一四角生成 OSD 线段。图片分析页的缩略图与大图共用 `src/web/src/utils/targetGeometry.js`：存在四个有效顶点就依序连线，否则沿用水平框绘制。两个 canvas 的内部尺寸与源图一致，再通过 CSS 与图片一起缩放；前端不再读取角度或重新计算旋转矩形。

## 接入文件

| 范围 | 入口 |
| --- | --- |
| 模型解码 | `src/nn/node/yolo_obb_decode_node.h/.cc` |
| Pipeline 与注册 | `src/nn/pipeline/detection_pipeline.h/.cc` |
| 通用结果与检测适配 | `src/nn/utils/net_utils.h`、`src/infer/AiCommon.h`、`src/infer/AiDetectorUnify.cc` |
| 跟踪来源关联 | `src/infer/AiTrackerUnify.cc`、`src/nn/utils/tracker/` |
| 几何绘制与 API 结果 | `src/util/GeometricPos.h/.cc`、结果 DTO 和告警输出链路 |
| 前端类型与参数 | `src/web/src/views/gam/countManagement/atomicModel/index.vue`、`modelConfig/ParamsConfig.vue` |
| 前端图片绘制 | `src/web/src/views/gam/imageAnalysis/index.vue`、`src/web/src/utils/targetGeometry.js` |
| 模型及算法模板 | 三个平台资源目录的 `model_template/` 与 `algorithm_template/` |

各平台只保留一份编号 `38874` 的算法模板，文件名为 `38874_yolo26_obb_20260914170000.json`。语言字典放在 `i18n/`，不能混入算法模板目录。该模板用于图片检测编排；导入匹配平台的模型后仍需选择模型并核对类别，模板中的 `object` 是占位类别。

本次接入保留已有模型管理页的导入与初始化流程，不附加平台类型判断、GPU 字典请求或批量更新操作。

## 验证与平台边界

开发验证至少覆盖以下内容，实际执行结果单独记录：

1. 已知旋转框的 0 度、90 度和一般角度；细长框、边界框以及非有限值、非法类别等异常输入。
2. 方形与非方形输入，居中/左上填充、拉伸和 ROI 坐标还原；归一化模型显式指定坐标单位。
3. 原有普通检测结果保持水平框链路；OBB API 输出四角，普通检测不输出该字段。
4. 视频检测经过跟踪后的来源关联，包括相同水平框的不同检测、漏检预测和重新匹配。
5. 后端标注图和前端四角绘制一致；模型管理页能初始化、列出模型并显示导入入口。
6. 三个平台资源 JSON 可解析，每个平台编号 `38874` 唯一，新增文件名可在 Windows 检出。

原生检查入口：

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests
scripts/format_check.sh
cd src/web
npm ci
npm run build
# build 的 prebuild 已包含 target-geometry:check
```

文档检查在仓库根目录执行 `npm run docs:verify`。

x86 使用 ONNX Runtime CPU 直接加载 `model.onnx`。Sophon 的模型需要对应芯片的 `.bmodel`，通过既有包装流程形成 `model.nn`；BM1688 与 CV186X 产物不能互用。此文没有提供模型转换或设备验收的成功声明。RKNN 端到端 OBB 输出契约尚未通过该适配器的设备验证，不因前端出现检测类型就宣称支持。

模型转换、远程执行和设备验证按 `AGENTS.md` 的任务准入及授权流程进行。设备验收需要固定本次程序、模型和资源身份，并分别核对图片、视频跟踪与绘制结果；本地单元测试不能替代这些结果。
