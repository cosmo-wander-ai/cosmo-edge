---
title: 图片检测 API 接入指南
description: 通过 HTTP API 上传图片、执行无区域编排、底库比对和双图比对，解析业务结论、命中样本与相似度分数。
prev:
  text: API 概览
  link: /reference/api
next:
  text: 字段级 API 参考
  link: /reference/api-fields
---

# 图片检测 API 接入指南

本文面向需要从业务系统上传图片并同步取得分析结果的集成方。检测、分类、关键点、分割、OCR、VLM、底库比对和双图比对复用同一组图片分析 API；实际能力由设备上已配置的图片场景决定。控制台操作见[图片分析与图片比对](../guide/image-analysis.md)，字段速查见[图片分析字段](api-fields.md#图片分析字段)。

## 接入前提

开始调用前，请确认：

- CosmoEdge 已部署并可通过 HTTP 或 HTTPS 访问。
- 设备上已有一个可用的**图片分析**算法。人脸检测算法的 Pipeline 至少需要包含人脸检测节点。
- 集成方具有有效的登录账号，并已按部署要求修改初始密码。
- 调用端能够保存登录返回的 `mtk`，并在同一登录身份下完成图片上传和检测。

本文使用以下占位符：

| 占位符 | 说明 |
| --- | --- |
| `BASE_URL` | CosmoEdge 服务地址，例如 `https://edge.example.com` |
| `MTK` | 登录成功后返回的令牌 |
| `ALGORITHM_CODE` | 图片分析算法 ID |
| `TASK_ID` | 调用方生成的图片任务 ID，建议使用 UUID |
| `UPLOAD_ID` | 图片上传完成后由服务端签发的会话 ID |

除登录接口外，本文中的请求都必须携带：

```http
mtk: <MTK>
```

业务成功由响应体中的 `resCode` 判断，而不是只看 HTTP 状态码：

```json
{
  "resCode": 1,
  "resMsg": []
}
```

`resCode` 为 `0` 时，应读取 `resMsg[]` 中的 `messageKey`、`msgText`、`details` 和 `recommendedAction`。

## 调用时序

一次完整接入包含以下步骤：

1. 登录并取得 `mtk`。
2. 查询图片分析算法，取得 `ALGORITHM_CODE`。
3. 创建图片分析任务。
4. 查询上传能力并上传图片，取得 `UPLOAD_ID`。
5. 调用图片检测接口并解析结果。
6. 不再使用任务时取消任务，释放模型资源。

同一个任务创建成功后可以连续检测多张图片，不需要每张图片都重新创建任务。每张图片都需要单独上传，并使用新的 `UPLOAD_ID`。

## 1. 登录

### 请求

```http
POST /gtw/cwai/login/DoLogin
Content-Type: application/json
```

```json
{
  "account": "<账号>",
  "pwd": "<密码的 32 位 MD5 十六进制值>"
}
```

`pwd` 传输的是密码的 MD5 值，不是明文。MD5 只属于当前兼容协议，不能替代传输加密；跨不可信网络部署时应使用 HTTPS 或受保护的管理网络。

### 成功响应

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "accountName": "api-user",
    "mtk": "<MTK>",
    "passwordChangeRequired": false
  }
}
```

后续请求使用 `resData.mtk`。如果 `passwordChangeRequired` 为 `true`，应先按设备安全策略修改密码，再开始业务接入。

## 2. 查询图片分析算法

### 请求

```http
POST /gtw/cwai/algorithm/page
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "algorithmUsage": "2",
  "algorithmName": "人脸",
  "supplier": "",
  "algorithmId": "",
  "algorithmCategory": "",
  "pageNum": 1,
  "pageSize": 100
}
```

`algorithmUsage: "2"` 表示图片分析算法，`"1"` 表示视频分析；场景列表的“数据源类型”查询使用同一字段，留空表示不限制类型。`algorithmName` 可以留空后由调用方选择，也可以填写部署时配置的算法名称。

### 响应

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "total": 1,
    "rows": [
      {
        "algorithmId": "7602",
        "algorithmName": "人脸识别算法",
        "algorithmUsage": "2",
        "runningStatus": 0,
        "models": []
      }
    ]
  }
}
```

后续接口中的 `algorithmCode` 应填写这里返回的 `resData.rows[].algorithmId`。不要把模型编码、原子动作编码或检测标签（例如 `face`）当作 `algorithmCode`。

不同部署中的算法 ID 和 Pipeline 可能不同。若只需要人脸框和置信度，应选择或创建只包含所需节点的图片分析算法；不要依赖示例中的 `7602` 恒定存在。

## 3. 创建图片分析任务

### 请求

```http
POST /gtw/cwai/aihost/PTaskCreate
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "mvDebug": "Cosmo-Debug",
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "algorithmUpdateTime": "<CURRENT_TIME_MILLIS>"
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `algorithmCode` | 是 | 图片分析算法 ID |
| `algorithmUpdateTime` | 是 | 当前时间的 13 位毫秒时间戳兼容字段；当前任务实际按 `algorithmCode` 从本地算法库加载 |
| `taskId` | 建议 | 调用方生成的全局唯一 ID；省略时默认等于 `algorithmCode` |
| `mvDebug` | 建议 | 当前图片分析客户端使用 `Cosmo-Debug` |
| `taskConfig` | 否 | 本次任务的参数与库绑定配置，不支持区域 |

生产集成建议显式提供唯一的 `taskId`。如果多个客户端都省略该字段，它们会使用同一个默认任务；其中一个客户端取消任务可能影响其他客户端。

### 成功响应

```json
{
  "resCode": 1,
  "resMsg": []
}
```

任务创建可能包含模型加载和 Pipeline 初始化。调用端建议为该请求设置不低于 120 秒的超时。重复创建同一个 `taskId` 会复用已创建任务，但调用方仍应管理好自己的任务生命周期。

## 4. 上传图片

### 4.1 查询设备上传能力

上传前调用：

```http
POST /gtw/cwai/atomic/model/uploadCapabilities
Content-Type: application/json
mtk: <MTK>

{}
```

调用方至少应检查以下响应字段：

| 字段 | 说明 |
| --- | --- |
| `maxChunkSize` | 单个分片最大字节数 |
| `availableForNewUploadsBytes` | 扣除安全储备和在途预留后的可接纳字节数 |
| `maxEncodedImageBytes` | 当前设备可接收的编码图片大小 |
| `maxImagePixels` | 当前设备可解码的最大像素数 |
| `resumable` | 是否支持断点续传 |
| `persistentAcrossRestart` | 上传会话是否可跨引擎重启恢复 |

这些值由当前设备资源和部署策略决定，客户端不能把某次查询结果写死为产品常量。

### 4.2 单分片上传

当图片不超过设备返回的 `maxChunkSize` 时，可以一次上传完成：

```bash
curl -X POST "${BASE_URL}/gtw/cwai/atomic/model/uploadTemp" \
  -H "mtk: ${MTK}" \
  -F "file=@face.jpg" \
  -F "purpose=image" \
  -F "chunkIndex=0" \
  -F "totalChunks=1" \
  -F "totalSize=<图片总字节数>" \
  -F "chunkSize=<图片总字节数>" \
  -F "clientRequestId=<本次上传的稳定 UUID>"
```

`totalSize` 和 `chunkSize` 必须使用图片的实际字节数，不能使用 Base64 长度或 multipart 请求总长度。

### 4.3 多分片上传

超过单分片上限时，按顺序调用同一接口：

- 第 0 片不传 `uploadId`，服务端创建会话。
- 后续分片携带服务端返回的 `uploadId`。
- 所有分片使用相同的文件名、`purpose`、`totalChunks`、`totalSize`、`clientRequestId` 和可选 `sha256`。
- `chunkIndex` 从 `0` 开始，并严格按照响应中的 `nextChunkIndex` 继续。
- 推荐分片大小为 8 MB，但必须以实时返回的 `maxChunkSize` 为上限。

完整 multipart 字段定义见[字段级 API 参考](api-fields.md#分片上传字段)。

### 上传成功响应

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "uploadId": "<UPLOAD_ID>",
    "nextChunkIndex": "1",
    "complete": true,
    "filePath": "upload://compatibility-alias"
  }
}
```

只有 `complete` 为 `true` 时才能进入检测。业务接口只应使用 `uploadId`；`filePath` 是兼容别名，不是服务器文件路径。

`UPLOAD_ID` 具有以下约束：

- 与创建上传会话的登录用户绑定，必须由同一身份消费。
- 被 `PTaskDetectPic` 成功认领后即消费，不能用于另一张图片或重复检测。
- 未使用的上传会话应调用 `/gtw/cwai/atomic/model/cancelUpload` 释放预留空间。

当前控制台接受 JPEG、PNG 和 BMP。最终是否能处理仍以目标设备的解码能力和 `uploadCapabilities` 返回值为准。

## 5. 执行图片检测

### 推荐请求：使用 `uploadId`

```http
POST /gtw/cwai/aihost/PTaskDetectPic
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "uploadId": "<UPLOAD_ID>",
  "resultMode": "business",
  "needRetImg": false
}
```

`taskId` 和 `algorithmCode` 必须与创建任务时保持一致。接口同步执行图片解码和推理，建议将客户端超时设置为不低于 60 秒，并根据模型耗时调整。

### 兼容请求：Base64 或 URL

接口还兼容 `imageBase64` 或 `imageUrl`：

```json
{
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "imageBase64": "<不带 data:image/... 前缀的原始 Base64>"
}
```

或者：

```json
{
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "imageUrl": "https://example.com/input/face.jpg"
}
```

`uploadId`、`imageBase64` 和 `imageUrl` 只能选择一种。JSON 请求体默认上限为 1 MB，因此生产接入和高清图片应使用 `uploadId`。使用 `imageUrl` 时，URL 必须可从 CosmoEdge 设备访问，下载失败会返回业务错误。

### 双图人脸 / 工服比对

选择内置场景 `93000002`（双图人脸比对）或 `93000058`（双图工服比对）创建图片任务，再向同一个接口提供两个输入。主输入是图片 A，`referenceImage` 是图片 B；每一侧分别选择 `uploadId`、`imageBase64`、`imageUrl` 中的一种。两个上传 ID 必须属于当前认证用户，只消费一次。

```json
{
  "taskId": "pair-example",
  "algorithmCode": "93000002",
  "uploadId": "<IMAGE_A_UPLOAD_ID>",
  "referenceImage": { "uploadId": "<IMAGE_B_UPLOAD_ID>" },
  "resultMode": "business",
  "needRetImg": true,
  "taskConfig": { "params": [{ "key": "pair.threshold", "value": "80" }] }
}
```

两张图片使用相同的模型和参数，分别执行检测、筛选、关键点（人脸）及特征提取。每张图片必须恰有一个符合条件的目标。双图场景无需底库，不支持区域、分支、多目标配对或 `legacy` 结果模式。原有单图任务和底库比对接口保持兼容。

成功时，`resData.comparison` 始终包含 `score`，取值为 0–100；低于阈值仍然返回实际分数。`pair.threshold` 留空表示只输出分数，返回 `decision: "score_only"`，不返回 `threshold`。省略参数则继续使用场景默认值；传入空字符串可以清除已保存的阈值。设置阈值时，分数大于等于阈值返回 `matched`，否则为 `not_matched`。

```json
{
  "nodeId": "comparison-node",
  "featureType": "face",
  "decision": "matched",
  "score": 87.25,
  "threshold": 80
}
```

图片 A 的目标和结果图片仍在 `targetList`、`fullPicture`；图片 B 使用 `referenceTargetList`、`referencePicture`。目标框是各自原图的像素坐标，可用于展示裁剪图。`needRetImg: false` 时不生成结果图片；`debug` 模式下节点记录的 `imageSide` 区分 A、B 和双方比对。

未检测到有效目标、多目标、无效特征、无效分数配置和解码失败均属于执行错误：响应 `resCode` 为失败，`resMsg` 给出具体原因，`resData.errorSide` 指出 `A`、`B` 或 `both`，且不返回成功的 `comparison.score`。有效的 0 分是一次成功比对，不能等同于这些错误。

分数是模型标定后的相似度，不是身份概率。工服场景复用人体外观特征模型，其分数表示人体外观相似程度，不能解释为“穿同款工服”的概率。

## 6. 解析编排结果

图片接口默认使用 `resultMode: "business"`，顺着配置的模型、筛选、判断和输出节点执行，返回 `schemaVersion: 2`。图片任务没有检测区域：不配置区域，不接受非空 `taskConfig.areas` 或 `shieldedAreas`，新响应没有 `areaList`。

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "schemaVersion": 2,
    "requestId": "image-001",
    "algorithmCode": "<ALGORITHM_CODE>",
    "status": "completed",
    "timestamp": "1770000000000",
    "fullPicture": "",
    "outputs": [
      {
        "nodeId": "output",
        "name": "Helmet check",
        "decision": "matched",
        "targetIds": [
          "detect:0"
        ],
        "matchedCount": 1
      }
    ],
    "targetList": [
      {
        "targetId": "detect:0",
        "sourceNodeId": "detect",
        "atomicCode": "<MODEL_CODE>",
        "decision": "matched",
        "rules": {
          "rule": "matched"
        },
        "filtered": false,
        "box": {
          "x": 120,
          "y": 80,
          "width": 160,
          "height": 180
        },
        "confidence": [
          {
            "label": "person",
            "confidence": 0.96
          }
        ]
      }
    ]
  }
}
```

业务方读取 `resData.outputs[]` 的结论，再用 `targetIds` 关联 `resData.targetList[]`。同一个目标经过分支时保持 `targetId`，`rules` 按节点 ID 保存判断。输出节点的 `output.targets` 默认为 `matched`；设为 `all` 可返回通过筛选的全部目标。没有判断节点的检测流水线以检测到目标作为命中。整图判断可以在目标数为零时仍命中，例如“目标数量等于 0”。

| 字段 | 说明 |
| --- | --- |
| `decision` | `matched` 命中、`not_matched` 未命中、`unknown` 证据不足；目标还可为 `not_evaluated`，双图无阈值时为 `score_only` |
| `box` | `x/y/width/height` 均为像素坐标 |
| `confidence[]` | 检测与分类输出的标签及置信度 |
| `attributes[]`、`texts[]` | 属性分类结果、OCR 文字 |
| `matchInfo` | 库比对分数、分组及命中信息；空库不能作为“未命中”证据 |
| `outputs[].reason` | 无法判断的原因：`no_comparable_samples` 或 `insufficient_evidence`；已知结论不附带此字段 |
| `landmark[]` | 关键点，兼容键名为 `xRatio/yRatio`，值为像素坐标 |
| `maskPolygon[]` | 分割轮廓，`xRatio/yRatio` 为归一化坐标 |
| `rules` | 每个判断节点的结果；缺少输入时保留 `unknown`，取反也不会变为命中 |
| `errorNodeId` | 执行失败节点；同时检查顶层 `resCode`，失败时不返回部分成功结论 |

`resultMode: "debug"` 额外返回节点耗时、输入/输出数量、跳过状态以及被筛除/未命中目标；`filtered`、`filterReason` 解释筛选结果。调试目标不能直接用作告警目标，应使用输出节点的 `targetIds`。

显式指定 `resultMode: "legacy"` 可临时获得旧版 `areaList` 包装；它仅用于协议兼容，内部仍执行同一套无区域编排。新客户端应使用默认业务响应。

`needRetImg: false` 关闭叠加图。开启时 `fullPicture` 可能为文件服务 URL 或 `data:image/jpeg;base64,...`，仅绘制输出选中的目标。

### 底库命中详情与业务结论

人脸、工服库比对在 `targetList[].matchInfo` 中返回比对证据。以下为一个工服库命中的字段示例，数值仅用于说明协议：

```json
{
  "setPicCount": 3,
  "matched": true,
  "matchDegree": 92.6,
  "matchId": "sample-001",
  "name": "示例工服样本",
  "groupId": "library-001",
  "groupName": "示例工服库",
  "baseImageUrl": "/sample-library-image.jpg",
  "personId": "sample-001"
}
```

| 字段 | 含义 |
| --- | --- |
| `setPicCount` | 实际参与有效比对的样本数；`0` 表示没有有效比对证据，不是库中展示的图片总数 |
| `matched` | 是否命中底库；与取反后的业务规则结论分别读取 |
| `matchDegree` | 最高有效比对分数，采用服务的 0–100 分尺度；不是检测置信度或概率 |
| `matchId` | 最佳匹配样本 ID |
| `name` | 人脸库中的人员姓名，或工服库中的样本图片名称 |
| `groupId`、`groupName` | 最佳匹配所属底库的 ID、名称 |
| `baseImageUrl` | 底库参考图片地址；与本次输入的结果图片 `fullPicture` 不同 |
| `personId`、`personCode` | 人员/样本标识及人员编号，在有对应数据时提供 |

`name`、`baseImageUrl`、`personId`、`personCode` 为空时省略，客户端应允许缺失。仅在 `setPicCount > 0` 时解释比对分数。有效比对未命中时，调试响应或配置 `output.targets=all` 可用于查看返回目标的最佳候选信息；候选不应展示为已命中。

例如“图片未穿工服”使用 `match.mode=unmatched`：命中工服库时 `matchInfo.matched=true`，但业务 `decision=not_matched`。默认业务响应仍保留该底库命中目标的详情，即使它不在输出节点的 `targetIds` 中。因此不要把 `targetList.length` 当作命中数，应读取 `outputs[].matchedCount`，并按 `targetIds` 确定业务目标。

### 无法判断与失败的边界

`resCode=1`、`status=completed` 只表示执行完成，业务输出仍可能是 `unknown`：

| 输出或错误 | 含义 |
| --- | --- |
| `decision=unknown`、`reason=no_comparable_samples` | 库绑定已提供，但没有样本产生有效比对；检查空库、无效/不兼容特征及模型分数配置 |
| `decision=unknown`、`reason=insufficient_evidence` | 规则缺少其他有效输入；通过 `debug` 节点记录、筛选原因和规则字段定位 |
| `decision=not_matched` | 业务规则未成立；与该规则选择的“命中/未命中”模式有关 |
| `resCode=0` | 配置或执行错误；读取 `resMsg[]`，以及存在时的 `errorNodeId`、`errorSide` |

未选择底库返回 `api.error.FaceLibraryNotConfigured` 或 `api.error.BodyLibraryNotConfigured`，不再用泛化的“无效的参数”表示。选择了库但没有有效样本，与没有配置库是两个不同状态。`unknown` 不会因取反变成 `matched`，也不能作为没有穿工服的证据。

### 参数与图片组件

每次请求可携带 `taskConfig.params: [{"key":"aiParam.person.confidence","value":"0.6"}]`。参数优先级为：模型默认值 → 节点配置 → 场景模板默认值 → 创建任务参数 → 本次请求参数。请求覆盖不会保存到任务中；`atomicCode` 和 `pair.featureType` 由节点配置决定，不能通过请求切换模型或特征类型。

图片组件包括检测、目标/整图分类、关键点、特征提取、OCR、DINO、SAM、整图/目标裁剪 VLM、目标筛选、目标判断、整图判断、条件分支、库比对和结果输出。目标裁剪使用模型检测框，不是用户配置区域。筛选支持类别、置信度、像素面积和最短边；不提供运动、区域或持续时间条件。

条件可引用 `aiOut.<label>.threshold`、`aiOut.attr.<category>`、`aiParam.<label>.confidence`、`picture.count`、`picture.matchedCount`、`match.score`、`match.matched`、`ocr.text` 和 `node.<flowActionId>`。节点引用必须位于当前分支的上游。条件分支只有命中才执行后续节点；未命中或无法判断时跳过。当前图支持单父节点与分叉，不支持合流。

VLM 用 `inputType=image`（默认）或 `targets` 选择整图/目标输入；分类默认 `targets`，可设为 `image`。库比对独立于特征提取：`match.libraryType=face/body`、`match.mode=matched/unmatched`，通过 `param.faceSet` 或 `param.workClothesSet` 绑定逗号分隔的库 ID，`param.limitScore` 范围 0–100。绑定库和所需模型必须实际存在于设备上。

例如为工服场景覆盖底库与阈值：

```json
{
  "taskConfig": {
    "params": [
      { "key": "param.workClothesSet", "value": "<LIBRARY_ID_1>,<LIBRARY_ID_2>" },
      { "key": "param.limitScore", "value": "70" }
    ]
  }
}
```

人脸场景使用 `param.faceSet`。当前库比对以最高分**大于**有效阈值判定命中；正的 `param.limitScore` 覆盖底库阈值，`0` 则沿用最佳候选所属底库的阈值。双图使用独立的 `pair.threshold`，按大于等于判定且允许留空只返回分数，不应混用这两组参数。

### 从视频模板生成图片模板

运行 `python tools/generate_picture_templates.py` 生成三个平台的对应图片模板，`--check` 校验产物是否最新。原视频模板保留。转换记录位于各资源目录的 `layout/picture-template-conversions.json`，包含源模板、图片模板 ID 和不转换的原因。

当前生成 13 个单图模板与 2 个双图模板，分别写入 `algorithm_template/`，同时向 `algorithm/` 写入同 ID 的内置场景，随资源包提供。完整列表见[内置模板与场景任务](../guide/image-analysis.md#内置模板与场景任务)。设备实际可用列表仍通过 `/gtw/cwai/algorithm/page` 查询。

可转换模板去掉解码、跟踪、持续时间与视频告警节点，将事件输出替换为图片结果输出。它们表达单张图片可判断的状态，不保留视频持续时间语义。含区域、绊线、离岗、历史计数等条件的模板不会转换。模板提供编排配置，不能替代对应芯片模型的安装和实测。图片输出不自动创建视频事件或触发外部推送。

## 7. 取消任务

完成一批图片检测且不再复用任务时，应释放模型和 Pipeline 资源：

```http
POST /gtw/cwai/aihost/PTaskCancle
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "mvDebug": "Cosmo-Debug",
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>"
}
```

成功响应：

```json
{
  "resCode": 1,
  "resMsg": []
}
```

建议在调用方的 `finally`、会话关闭或空闲回收流程中执行取消操作。不要在其他请求仍使用同一 `taskId` 推理时取消任务。

## 8. 取消未消费的上传

图片上传完成后，如果业务校验失败、任务创建失败或决定不再检测，应取消上传会话：

```http
POST /gtw/cwai/atomic/model/cancelUpload
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "uploadId": "<UPLOAD_ID>"
}
```

已被检测接口消费的 `uploadId` 不需要再次取消。

## 9. 错误处理

失败响应示例：

```json
{
  "resCode": 0,
  "resMsg": [
    {
      "msgCode": "IMAGE_INPUT_TOO_LARGE",
      "messageKey": "api.error.imageInputTooLarge",
      "msgText": "Encoded image exceeds the device decoding input limit",
      "details": {
        "actualBytes": 12582912,
        "limitBytes": 8388608,
        "resource": "encoded-image",
        "purpose": "image"
      },
      "retryable": false,
      "recommendedAction": "RESIZE_OR_RECOMPRESS_IMAGE"
    }
  ]
}
```

常见问题及处理方式：

| `messageKey` 或错误类型 | 常见原因 | 建议处理 |
| --- | --- | --- |
| `api.error.ActionAlgLoadFailed` | `algorithmCode` 不存在或算法资源不可用 | 重新查询图片分析算法并检查模型状态 |
| `api.error.FaceLibraryNotConfigured` / `api.error.BodyLibraryNotConfigured` | 未绑定对应底库 | 选择人脸/工服分组，分别设置 `param.faceSet` / `param.workClothesSet` |
| `api.error.PicturePairInputRequired` | 双图场景缺少 A 或 B | 两侧分别提供一个图片输入 |
| `api.error.PicturePairUnexpectedReference` | 单图场景收到图片 B | 改用双图场景或移除 `referenceImage` |
| `api.error.PicturePairNoTarget` / `api.error.PicturePairMultipleTargets` | 某侧有效目标数不是 1 | 按 `errorSide` 检查图片、检测和筛选条件 |
| `api.error.PicturePairInvalidFeature` | 空特征、无效值或维度不一致 | 检查特征提取模型与输入 |
| `api.error.PicturePairInvalidCalibration` | 模型比对分数配置无效 | 修复特征模型的分数标定配置 |
| `api.error.NotCreated` | 未创建任务、任务已取消或 `taskId` 不一致 | 使用相同参数重新调用 `PTaskCreate` |
| `api.error.TaskCreateFailed` | 模型或 Pipeline 初始化失败 | 检查模型状态和设备日志，避免无界重试 |
| `api.error.InvalidParam` | 图片来源冲突或字段不合法 | 确保三种图片来源只传一种，并核对字段类型 |
| `api.error.ImageDecodeFailed` | 图片损坏或格式不受设备支持 | 重新编码为受支持格式 |
| `api.error.ImageDownloadFailed` | 设备无法访问 `imageUrl` | 检查网络和 URL，或改用 `uploadId` |
| `IMAGE_INPUT_TOO_LARGE` | 编码图片超过输入上限 | 按 `recommendedAction` 压缩或缩放图片 |
| `IMAGE_RESOLUTION_TOO_LARGE` | 解码后的像素数超过上限 | 降低分辨率后重试 |
| `STORAGE_RESERVE_REACHED` | 安全可用磁盘空间不足 | 释放空间，不要立即无界重试 |
| HTTP `401` | `mtk` 缺失、失效或不属于当前会话 | 重新登录并重建上传会话 |
| HTTP `413` | JSON 或单次 multipart 请求过大 | 使用分片上传或减小请求体 |

客户端应优先按 `messageKey` 和 `recommendedAction` 分支处理。普通业务错误的 `msgCode` 可能是数字兼容码，不应只依赖其显示文本。

## 10. 生产接入建议

- 使用 HTTPS 或受保护的管理网络，避免令牌和兼容密码摘要暴露。
- 每个业务会话使用唯一 `taskId`，并明确任务所有者和回收时机。
- 一个任务创建后批量复用，避免为每张图片重复加载模型。
- 对上传、创建任务和推理分别设置超时；不要对不可重试错误无限重试。
- 每次上传前以实时能力为准，限制客户端并发和待处理队列。
- 记录业务请求 ID、`taskId`、`algorithmCode`、图片摘要和服务端错误字段，但不要记录密码、`mtk`、完整 Base64 图片或人脸特征。
- 人脸图片和检测结果可能属于敏感个人信息，应按部署地区的隐私、留存和访问控制要求处理。

更多通用限制和字段定义请参阅 [API 概览](api.md) 与 [字段级 API 参考](api-fields.md)。
