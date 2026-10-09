---
title: Image Detection API Integration
description: Use the HTTP API to sign in, create an image-analysis task, upload an image, run face or general object detection, and parse structured results.
prev:
  text: API Overview
  link: /en/reference/api
next:
  text: API Fields
  link: /en/reference/api-fields
---

# Image Detection API Integration

This guide is for applications that upload an image and synchronously receive detection results. Face detection, general object detection, landmarks, and segmentation use the same image-analysis APIs. The algorithm Pipeline configured on the target device determines the actual capability.

## Prerequisites

Before calling the APIs, confirm that:

- CosmoEdge is deployed and reachable over HTTP or HTTPS.
- The device has an available **Image Analysis** algorithm. A face-detection Pipeline must contain at least a face detector node.
- The integrator has a valid account and has changed the initial password as required by the deployment.
- The client can retain the `mtk` returned at sign-in and use the same identity for image upload and detection.

This guide uses the following placeholders:

| Placeholder | Meaning |
| --- | --- |
| `BASE_URL` | CosmoEdge service URL, for example `https://edge.example.com` |
| `MTK` | Token returned by a successful sign-in |
| `ALGORITHM_CODE` | Image-analysis algorithm ID |
| `TASK_ID` | Client-generated image task ID; a UUID is recommended |
| `UPLOAD_ID` | Server-issued session ID returned after the image upload completes |

Except for sign-in, every request in this guide must include:

```http
mtk: <MTK>
```

Determine business success from `resCode` in the response body, not only from the HTTP status:

```json
{
  "resCode": 1,
  "resMsg": []
}
```

When `resCode` is `0`, inspect `messageKey`, `msgText`, `details`, and `recommendedAction` in `resMsg[]`.

## Request Flow

A complete integration performs these steps:

1. Sign in and obtain an `mtk`.
2. Query image-analysis algorithms and obtain `ALGORITHM_CODE`.
3. Create an image-analysis task.
4. Query upload capabilities, upload an image, and obtain `UPLOAD_ID`.
5. Call the image-detection endpoint and parse its result.
6. Cancel the task when it is no longer needed to release model resources.

After one task is created, it can process multiple images without being recreated for every image. Upload every image separately and use a new `UPLOAD_ID` for each detection.

## 1. Sign In

### Request

```http
POST /gtw/cwai/login/DoLogin
Content-Type: application/json
```

```json
{
  "account": "<account>",
  "pwd": "<32-character hexadecimal MD5 of the password>"
}
```

`pwd` carries the MD5 value of the password, not plaintext. MD5 is only part of the current compatibility protocol and does not provide transport security. Use HTTPS or a protected management network across untrusted networks.

### Successful Response

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

Use `resData.mtk` in later requests. If `passwordChangeRequired` is `true`, change the password according to the device security policy before starting business integration.

## 2. Query Image-Analysis Algorithms

### Request

```http
POST /gtw/cwai/algorithm/page
Content-Type: application/json
mtk: <MTK>
```

```json
{
  "algorithmUsage": "2",
  "algorithmName": "face",
  "supplier": "",
  "algorithmId": "",
  "algorithmCategory": "",
  "pageNum": 1,
  "pageSize": 100
}
```

`algorithmUsage: "2"` selects image-analysis algorithms. Leave `algorithmName` empty to let the client choose, or use the name configured for this deployment.

### Response

```json
{
  "resCode": 1,
  "resMsg": [],
  "resData": {
    "total": 1,
    "rows": [
      {
        "algorithmId": "7602",
        "algorithmName": "Face Recognition Algorithm",
        "algorithmUsage": "2",
        "runningStatus": 0,
        "models": []
      }
    ]
  }
}
```

Use `resData.rows[].algorithmId` as `algorithmCode` in the following APIs. Do not use a model code, atomic-action code, or detection label such as `face` as `algorithmCode`.

Algorithm IDs and Pipelines differ by deployment. If the application needs only face boxes and confidence values, select or create an image-analysis algorithm containing only the required nodes. Do not assume that the example ID `7602` always exists.

## 3. Create an Image-Analysis Task

### Request

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

| Field | Required | Description |
| --- | --- | --- |
| `algorithmCode` | Yes | Image-analysis algorithm ID |
| `algorithmUpdateTime` | Yes | Current 13-digit millisecond timestamp compatibility field; the current task loads the local algorithm by `algorithmCode` |
| `taskId` | Recommended | Globally unique client-generated ID; defaults to `algorithmCode` when omitted |
| `mvDebug` | Recommended | The current image-analysis client uses `Cosmo-Debug` |
| `taskConfig` | No | Per-task parameters and library bindings; regions are unsupported |

Production integrations should provide a unique `taskId`. If multiple clients omit it, they share the default task, and one client may affect another by cancelling that task.

### Successful Response

```json
{
  "resCode": 1,
  "resMsg": []
}
```

Task creation may load models and initialize the Pipeline. Use a timeout of at least 120 seconds. Recreating the same `taskId` reuses an existing task, but each client should still manage its own task lifecycle.

## 4. Upload an Image

### 4.1 Query Device Upload Capabilities

Call this endpoint before uploading:

```http
POST /gtw/cwai/atomic/model/uploadCapabilities
Content-Type: application/json
mtk: <MTK>

{}
```

At minimum, inspect these response fields:

| Field | Description |
| --- | --- |
| `maxChunkSize` | Maximum bytes in one chunk |
| `availableForNewUploadsBytes` | Admissible bytes after safety reserve and in-flight reservations |
| `maxEncodedImageBytes` | Maximum encoded image size accepted by the current device |
| `maxImagePixels` | Maximum decoded pixel count |
| `resumable` | Whether resumable uploads are supported |
| `persistentAcrossRestart` | Whether upload sessions survive an engine restart |

These values depend on current device resources and deployment policy. Do not hard-code the result of an earlier query as a product constant.

### 4.2 Single-Chunk Upload

If the image does not exceed `maxChunkSize`, upload it in one request:

```bash
curl -X POST "${BASE_URL}/gtw/cwai/atomic/model/uploadTemp" \
  -H "mtk: ${MTK}" \
  -F "file=@face.jpg" \
  -F "purpose=image" \
  -F "chunkIndex=0" \
  -F "totalChunks=1" \
  -F "totalSize=<total image bytes>" \
  -F "chunkSize=<total image bytes>" \
  -F "clientRequestId=<stable UUID for this upload>"
```

`totalSize` and `chunkSize` must be the actual image byte count, not the Base64 length or total multipart request length.

### 4.3 Multi-Chunk Upload

For an image larger than the single-chunk limit, call the same endpoint sequentially:

- Chunk 0 omits `uploadId`; the server creates a session.
- Later chunks include the server-issued `uploadId`.
- Every chunk uses the same filename, `purpose`, `totalChunks`, `totalSize`, `clientRequestId`, and optional `sha256`.
- `chunkIndex` starts at `0`; continue strictly from the returned `nextChunkIndex`.
- The recommended chunk size is 8 MB, bounded by the live `maxChunkSize`.

See [API Fields](api-fields.md#chunk-upload-fields) for the complete multipart field definition.

### Successful Upload Response

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

Proceed to detection only when `complete` is `true`. Business APIs must use `uploadId`; `filePath` is a compatibility alias, not a server filesystem path.

`UPLOAD_ID` has these constraints:

- It is bound to the authenticated user that created the upload session and must be consumed by the same identity.
- After `PTaskDetectPic` successfully claims it, it is consumed and cannot be reused for another image or another detection.
- Cancel an unused session through `/gtw/cwai/atomic/model/cancelUpload` to release reserved space.

The current console accepts JPEG, PNG, and BMP. Actual support remains subject to the target device decoder and the values returned by `uploadCapabilities`.

## 5. Run Image Detection

### Recommended Request: `uploadId`

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

`taskId` and `algorithmCode` must match task creation. The endpoint decodes and runs inference synchronously. Use a client timeout of at least 60 seconds and adjust it for the deployed model.

### Compatibility Requests: Base64 or URL

The endpoint also accepts `imageBase64` or `imageUrl`:

```json
{
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "imageBase64": "<raw Base64 without a data:image/... prefix>"
}
```

or:

```json
{
  "taskId": "<TASK_ID>",
  "algorithmCode": "<ALGORITHM_CODE>",
  "imageUrl": "https://example.com/input/face.jpg"
}
```

Choose exactly one of `uploadId`, `imageBase64`, and `imageUrl`. JSON request bodies are limited to 1 MB by default, so production integrations and high-resolution images should use `uploadId`. With `imageUrl`, the URL must be reachable from the CosmoEdge device.

### Two-image face / workwear comparison

Create a picture task using built-in scene `93000002` (two-image face comparison) or `93000058` (two-image workwear comparison), then send two inputs to the same endpoint. The primary input is image A; `referenceImage` is image B. Each side accepts exactly one of `uploadId`, `imageBase64`, or `imageUrl`. Both upload IDs must belong to the authenticated user and are consumed once.

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

Both images use the same models and parameters for detection, filtering, landmarks (faces), and feature extraction. Each image must contain exactly one eligible target. Pair workflows need no library and do not support regions, branches, multi-target pairing, or `legacy` results. Existing single-image and library-comparison requests remain compatible.

Successful responses always include `resData.comparison.score` on a 0–100 scale, even below the threshold. An empty `pair.threshold` requests scores only: `decision` is `score_only` and `threshold` is omitted. Omitting the parameter preserves scene defaults; an explicit empty string clears a stored threshold. When a threshold is configured, scores greater than or equal to it produce `matched`; lower scores produce `not_matched`.

```json
{
  "nodeId": "comparison-node",
  "featureType": "face",
  "decision": "matched",
  "score": 87.25,
  "threshold": 80
}
```

Image A uses `targetList` and `fullPicture`; image B uses `referenceTargetList` and `referencePicture`. Target boxes contain pixel coordinates in their respective original images, suitable for displaying crops. `needRetImg: false` skips result image generation. In `debug` mode, node traces include `imageSide` to distinguish A, B, and the comparison of both.

No eligible target, multiple targets, invalid features, invalid score calibration, and decode failures are execution errors. The response has a failed `resCode`, a specific reason in `resMsg`, and `resData.errorSide` of `A`, `B`, or `both`; no successful `comparison.score` is returned. A valid score of zero is a successful comparison and is distinct from an error.

Scores represent calibrated model similarity, not identity probability. The workwear scene reuses body appearance features; its score is not the probability of wearing the same uniform.

## 6. Parse Workflow Results

The default `resultMode: "business"` executes the configured models, filters, rules and output nodes, returning `schemaVersion: 2`. Image tasks have no regions. Nonempty `taskConfig.areas` or `shieldedAreas` are rejected; the new response has no `areaList`.

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

Read each decision in `resData.outputs[]` and resolve its `targetIds` against `resData.targetList[]`. IDs remain stable through forks; `rules` preserves decisions by node ID. `output.targets` defaults to `matched`; `all` includes all unfiltered targets. A detector without a rule matches when it finds targets. An image rule can match with zero targets, for example a count-equals-zero condition.

| Field | Meaning |
| --- | --- |
| `decision` | `matched`, `not_matched`, or `unknown` (insufficient evidence); targets can also be `not_evaluated` |
| `box` | Pixel `x/y/width/height` |
| `confidence[]` | Detector and classifier labels and confidence |
| `attributes[]`, `texts[]` | Attribute classifications and OCR text |
| `matchInfo` | Library scores, group and match information; an empty library is unknown, not an unmatched result |
| `landmark[]` | Pixel coordinates using the compatibility keys `xRatio/yRatio` |
| `maskPolygon[]` | Normalized `xRatio/yRatio` segmentation contours |
| `rules` | Decisions by node ID; missing evidence stays unknown even under NOT |
| `errorNodeId` | Failed node; check top-level `resCode` as well. Failure clears partial business outputs |

`resultMode: "debug"` additionally returns node timing, input/output counts, skipped nodes, filtered targets and unmatched targets. `filtered` and `filterReason` explain filtering. Use output `targetIds`, rather than all debug targets, for business decisions.

Explicit `resultMode: "legacy"` temporarily preserves the old `areaList` response wrapper, while running the same region-free workflow. New clients should use business mode.

Set `needRetImg: false` to disable the annotated image. Otherwise `fullPicture` may be a file-service URL or `data:image/jpeg;base64,...`; only selected output targets are drawn.

### Parameters and Image Components

Requests accept `taskConfig.params: [{"key":"aiParam.person.confidence","value":"0.6"}]`. Precedence is model defaults → node configuration → scenario defaults → task creation parameters → request overrides. Overrides never persist into another request and cannot change `atomicCode`.

Components include detection, target/whole-image classification, landmarks, features, OCR, DINO, SAM, whole-image/target-crop VLM, target filtering, target rules, image rules, conditional branches, library matching and result output. Crops use model detection boxes, not user-defined regions. Filtering supports labels, confidence, pixel area and shortest side; motion, region and duration conditions are unavailable.

Rules may reference `aiOut.<label>.threshold`, `aiOut.attr.<category>`, `aiParam.<label>.confidence`, `picture.count`, `picture.matchedCount`, `match.score`, `match.matched`, `ocr.text`, and `node.<flowActionId>`. Referenced nodes must be ancestors on the current branch. Conditional branches execute their descendants only when matched; unknown and false skip them. Graphs support single parents and forks, but not joins.

VLM uses `inputType=image` (default) or `targets`; classification defaults to `targets` and supports `image`. Library matching is separate from feature extraction: select `match.libraryType=face/body` and `match.mode=matched/unmatched`; bind comma-separated library IDs with `param.faceSet` or `param.workClothesSet`. `param.limitScore` ranges from 0 to 100. Required models and libraries must exist on the device.

### Video-to-image Templates

Run `python tools/generate_picture_templates.py` to generate templates for the three platforms, or pass `--check` to verify reproducibility. Original video templates are retained. Each resource directory contains `layout/picture-template-conversions.json`, listing source IDs, image IDs and exclusions.

Compatible templates remove decoding, tracking and sensitivity history, replacing video event output with image result output. Their meaning is a single-frame state, not a sustained video event. Region, tripwire, absence and historical counting workflows are excluded. Template availability does not imply that the corresponding chip-specific model is installed or device-validated. Image result output does not automatically create video events or send external notifications.

## 7. Cancel the Task

After a batch has finished and the task will not be reused, release its model and Pipeline resources:

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

Successful response:

```json
{
  "resCode": 1,
  "resMsg": []
}
```

Call cancellation from the client's `finally`, session-close, or idle-reaper path. Do not cancel a task while another request is still running inference with the same `taskId`.

## 8. Cancel an Unused Upload

If validation fails, task creation fails, or detection is no longer needed after an upload completes, cancel the upload session:

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

An `uploadId` already consumed by the detection endpoint does not need another cancellation.

## 9. Error Handling

Example failure response:

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

Common failures:

| `messageKey` or error | Likely cause | Recommended handling |
| --- | --- | --- |
| `api.error.ActionAlgLoadFailed` | `algorithmCode` does not exist or its resources are unavailable | Query image-analysis algorithms again and check model status |
| `api.error.NotCreated` | Task was not created, was cancelled, or uses a different `taskId` | Call `PTaskCreate` again with matching values |
| `api.error.TaskCreateFailed` | Model or Pipeline initialization failed | Inspect model status and device logs; do not retry without bounds |
| `api.error.InvalidParam` | Conflicting image sources or invalid fields | Send exactly one image source and verify field types |
| `api.error.ImageDecodeFailed` | Corrupt image or unsupported format | Re-encode the image to a supported format |
| `api.error.ImageDownloadFailed` | The device cannot reach `imageUrl` | Check network access or use `uploadId` |
| `IMAGE_INPUT_TOO_LARGE` | Encoded image exceeds the input limit | Compress or resize according to `recommendedAction` |
| `IMAGE_RESOLUTION_TOO_LARGE` | Decoded pixel count exceeds the limit | Reduce image resolution and retry |
| `STORAGE_RESERVE_REACHED` | Safe disk space is insufficient | Free disk space; do not retry immediately without bounds |
| HTTP `401` | `mtk` is missing or expired | Sign in again and create a new upload session |
| HTTP `413` | JSON or one multipart request is too large | Use chunked upload or reduce the request body |

Branch primarily on `messageKey` and `recommendedAction`. For ordinary business errors, `msgCode` may be a numeric compatibility code; do not depend only on displayed error text.

## 10. Production Guidance

- Use HTTPS or a protected management network to protect tokens and the compatibility password digest.
- Give each business session a unique `taskId` and define its owner and cleanup time.
- Reuse one created task for a batch instead of loading the model for every image.
- Set separate timeouts for upload, task creation, and inference; never retry non-retryable errors without bounds.
- Use live upload capabilities, and bound client concurrency and pending queues.
- Log a business request ID, `taskId`, `algorithmCode`, an image digest, and server error fields. Never log passwords, `mtk`, full Base64 images, or face features.
- Face images and detection results may be sensitive personal information. Apply the privacy, retention, and access-control requirements of the deployment jurisdiction.

For common limits and field definitions, see [API Overview](api.md) and [API Fields](api-fields.md).
