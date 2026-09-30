---
title: 图片检测 API 接入指南
description: 通过 HTTP API 登录、创建图片分析任务、上传图片、执行人脸或通用目标检测并解析结构化结果。
prev:
  text: API 概览
  link: /reference/api
next:
  text: 字段级 API 参考
  link: /reference/api-fields
---

# 图片检测 API 接入指南

本文面向需要从业务系统上传图片并同步取得检测结果的集成方。人脸检测、通用目标检测、关键点和分割任务复用同一组图片分析 API；实际能力由设备上已配置的图片分析算法决定。

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

`algorithmUsage: "2"` 表示图片分析算法。`algorithmName` 可以留空后由调用方选择，也可以填写部署时配置的算法名称。

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
| `taskConfig` | 否 | 本次任务的参数、区域和人脸库等覆盖配置 |

生产集成建议显式提供唯一的 `taskId`。如果多个客户端都省略该字段，它们会使用同一个默认任务；其中一个客户端取消任务可能影响其他客户端。

### 成功响应

```json
{
  "resCode": 1,
  "resMsg": []
}
```

任务创建可能包含模型加载和 Pipeline 初始化。调用端建议为该请求设置不低于 120 秒的超时。重复创建同一个 `taskId` 会复用已创建任务，但调用方仍应管理好自己的任务生命周期。

### 人脸库比对参数

此功能需要部署支持图片人脸库比对的版本，并使用包含 `PA_00005`、`featureInput=0` 和特征模型 `1000005` 的图片算法。仅有人脸检测框不代表已经识别出人员。该特征模型需要五点人脸关键点；流程没有提供完整关键点时，会调用已安装的 `1000016` 模型补齐。

先查询人脸库，选择响应 `resData.faceLibList[]` 中的 `id`，不要把库名称当作 ID。结果使用分页，库数量超过一页时应继续查询。

```http
POST /gtw/cwai/Library/QueryFaceLibInfo
Content-Type: application/json
mtk: <MTK>
```

```json
{ "pageNum": 1, "pageSize": 100 }
```

创建图片任务时传入完整比对配置。下面示例选择两个库，阈值为 70：

```json
{
  "mvDebug": "Cosmo-Debug",
  "taskId": "<FACE_TASK_ID>",
  "algorithmCode": "<FACE_ALGORITHM_ID>",
  "algorithmUpdateTime": "<CURRENT_TIME_MILLIS>",
  "taskConfig": {
    "params": [
      { "key": "param.faceCompare", "value": "1" },
      { "key": "param.faceSet", "value": "<LIBRARY_ID_A>,<LIBRARY_ID_B>" },
      { "key": "param.limitScore", "value": "70" }
    ]
  }
}
```

这三个 `value` 均为字符串；参数含义和结果字段见[图片人脸库比对](api-fields.md#图片人脸库比对)。`param.faceCompare=0` 表示只提取特征，不做底库比对。省略该开关时默认不比对。

算法返回的 `algorithmMetadata` 可能没有完整列出上述参数；第三方应按此接口约定传入。网页当前选择的库、开关和阈值是页面临时状态，没有对外读取该页面状态的接口；客户端应保存自己的配置。

### 同一张图片调用两个算法

标准图片接口每次分析一个算法。客户系统可以先分别调用 `PTaskCreate`，创建两个不同 `taskId` 的任务，各自设置算法和参数，再分别调用 `PTaskDetectPic`：

```json
{
  "taskId": "<TASK_ID_A>",
  "algorithmCode": "<ALGORITHM_ID_A>",
  "imageUrl": "https://images.example.com/input.jpg"
}
```

```json
{
  "taskId": "<TASK_ID_B>",
  "algorithmCode": "<ALGORITHM_ID_B>",
  "imageUrl": "https://images.example.com/input.jpg"
}
```

将示例 URL 替换为设备可访问且内容固定的图片地址，也可以分别提交同一份 `imageBase64`。使用推荐的分片上传时，需为两次分析分别上传同一张图片，使用不同 `clientRequestId` 获得两个 `uploadId`；同一个 `uploadId` 只能消费一次。

两个请求可以由客户端并发发送，但实际执行时间受模型调度、显存和设备负载影响，不保证同时完成。分别处理每个响应的 `resCode`，使用自己的业务图片 ID 和 `taskId` 关联结果；一个算法失败不应覆盖另一个算法的结果。等待各自请求结束后，分别取消两个任务。当前网页仍为单算法选择，标准接口也不会自动汇总两份结果。

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
  "uploadId": "<UPLOAD_ID>"
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

## 6. 解析检测结果

### 响应示例

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "algorithmCode": "7602",
    "timestamp": "1787068800123",
    "fullPicture": "data:image/jpeg;base64,/9j/4AAQ...",
    "areaList": [
      {
        "areaId": "-1",
        "areaName": "default",
        "bDetected": false,
        "targetList": [
          {
            "box": {
              "x": 120,
              "y": 80,
              "width": 160,
              "height": 180
            },
            "confidence": [
              {
                "label": "face",
                "confidence": 0.96
              }
            ],
            "landmark": [
              {"xRatio": 158.0, "yRatio": 132.0},
              {"xRatio": 232.0, "yRatio": 131.0}
            ]
          }
        ]
      }
    ]
  }
}
```

当前 HTTP 响应中的目标列表位于：

```text
resData.areaList[].targetList[]
```

调用方可以将所有区域的 `targetList` 合并后得到整张图片的目标集合。不要依赖未出现在当前 JSON 响应中的顶层 `resData.targetList`。

### 目标字段

| 字段 | 说明 |
| --- | --- |
| `box.x`、`box.y` | 检测框左上角像素坐标 |
| `box.width`、`box.height` | 检测框像素宽高 |
| `confidence[]` | 检测和后续分类节点输出；每项包含 `label` 和 `confidence` |
| `bLogicResult` | 仅 Pipeline 包含逻辑判断时可能返回，表示该目标是否满足逻辑条件 |
| `landmark[]` | 关键点节点输出；当前兼容字段名为 `xRatio`、`yRatio`，值按像素坐标使用 |
| `maskPolygon[]` | 分割轮廓；使用 `xRatio`、`yRatio`，值为归一化坐标 |
| `featurePreview` | 特征向量前若干项的调试预览，不是可用于业务比对的完整特征 |

人脸数量应按目标列表统计，例如：

```javascript
const targets = (response.resData.areaList || [])
  .flatMap(area => area.targetList || [])
const faces = targets.filter(target =>
  (target.confidence || []).some(item => item.label === 'face')
)
console.log('face count:', faces.length)
```

不要只使用 `area.bDetected` 判断是否检测到人脸。该字段表示区域逻辑是否触发；没有逻辑节点的纯检测 Pipeline 即使返回了目标，也可能为 `false`。

`fullPicture` 是叠加检测框后的整图。配置了文件服务时它可能是 URL；没有可用文件服务时会返回 `data:image/jpeg;base64,...`。当前 JSON 协议没有提供稳定的关闭该字段开关，客户端应允许两种返回形式并按需忽略。

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
