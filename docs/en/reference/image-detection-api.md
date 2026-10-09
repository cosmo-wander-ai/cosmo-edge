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
| `taskConfig` | No | Per-task parameter, region, and face-gallery overrides |

Production integrations should provide a unique `taskId`. If multiple clients omit it, they share the default task, and one client may affect another by cancelling that task.

### Successful Response

```json
{
  "resCode": 1,
  "resMsg": []
}
```

Task creation may load models and initialize the Pipeline. Use a timeout of at least 120 seconds. Recreating the same `taskId` reuses an existing task, but each client should still manage its own task lifecycle.

### Face-Library Matching Parameters

Deploy a version supporting image face-library matching and select an image Pipeline with `PA_00005`, `featureInput=0`, and embedding model `1000005`. A face box alone does not identify a person. This embedding model requires five face landmarks; when the Pipeline does not provide complete landmarks, the installed `1000016` model supplies them.

Query the available libraries and select their `id` values from `resData.faceLibList[]`, rather than their display names. Continue pagination if the libraries span multiple pages.

```http
POST /gtw/cwai/Library/QueryFaceLibInfo
Content-Type: application/json
mtk: <MTK>
```

```json
{ "pageNum": 1, "pageSize": 100 }
```

Send the complete matching configuration when creating the task. This example selects two libraries and a threshold of 70:

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

All three values are strings. See [Image Face Library Matching](api-fields.md#image-face-library-matching) for parameter and result definitions. `param.faceCompare=0` extracts features without library matching; omitting the switch also leaves matching disabled.

An algorithm's `algorithmMetadata` may not describe all these parameters. Integrators should send them according to this API contract. The console's currently selected libraries, switch, and threshold are temporary page state, with no API for reading that state; clients should retain their own configuration.

### Analyzing One Image with Two Algorithms

Each standard image request runs one algorithm. First call `PTaskCreate` separately for two distinct `taskId` values, with each algorithm's own parameters, then call `PTaskDetectPic` separately:

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

Replace the example URL with an immutable image reachable from the device, or send the same `imageBase64` separately. With the recommended staged upload, upload the image separately for each analysis using distinct `clientRequestId` values to obtain two `uploadId` values. Each `uploadId` can be consumed only once.

Clients may send the requests concurrently, but model scheduling, device memory, and load determine actual execution time; simultaneous completion is not guaranteed. Check each response's `resCode` independently and correlate results with the client's image ID and `taskId`. A failure from one algorithm must not overwrite the other result. Cancel each task after its requests finish. The current console selects one algorithm, and the standard endpoint does not aggregate the two results.

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
  "uploadId": "<UPLOAD_ID>"
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

## 6. Parse Detection Results

### Example Response

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

The current HTTP response exposes targets at:

```text
resData.areaList[].targetList[]
```

Merge the `targetList` values from all areas to obtain the targets for the whole image. Do not depend on a top-level `resData.targetList`, which is not emitted by the current JSON response.

### Target Fields

| Field | Description |
| --- | --- |
| `box.x`, `box.y` | Pixel coordinates of the bounding-box top-left corner |
| `box.width`, `box.height` | Bounding-box width and height in pixels |
| `confidence[]` | Detector and downstream classifier outputs; each item contains `label` and `confidence` |
| `bLogicResult` | May appear when the Pipeline has a logic node; indicates whether the target meets its condition |
| `landmark[]` | Landmark output; current compatibility keys are `xRatio` and `yRatio`, but the values are used as pixel coordinates |
| `maskPolygon[]` | Segmentation contour using normalized `xRatio` and `yRatio` values |
| `featurePreview` | Debug preview of the first feature-vector values, not a complete business-comparison feature |

Count faces from the target list, for example:

```javascript
const targets = (response.resData.areaList || [])
  .flatMap(area => area.targetList || [])
const faces = targets.filter(target =>
  (target.confidence || []).some(item => item.label === 'face')
)
console.log('face count:', faces.length)
```

Do not use only `area.bDetected` to determine whether a face was found. That field represents an area-logic trigger. A pure detector Pipeline without a logic node may return targets while the field is `false`.

`fullPicture` is the full image with detection overlays. It may be a URL when a file service is configured, or a `data:image/jpeg;base64,...` value when no file service is available. The current JSON protocol has no stable switch to disable this field, so clients should accept both forms and ignore it when it is not needed.

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
