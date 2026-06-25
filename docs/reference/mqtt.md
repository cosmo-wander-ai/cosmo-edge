---
title: MQTT 接入参考
description: 当前 MQTT topic、外层消息结构、注册、心跳、下发请求和响应格式说明。
prev:
  text: 字段级 API 参考
  link: /reference/api-fields
next:
  text: HTTP Webhook 参考
  link: /reference/webhook
---

# MQTT 接入参考

当前 MQTT 实现位于：

```text
src/network/mqtt
src/service/network/impl/MqttLifecycleServiceImpl.cc
```

运行时打包的 HTML 参考位于：

```text
data/Interface/mqtt_v1.0.html
```

## Topic

| 方向 | Topic | 说明 |
| --- | --- | --- |
| 设备 -> 平台 | `/d2p/aibox` | 注册消息和普通响应 |
| 设备 -> 平台 | `/d2p/aibox/heartbeat` | 心跳消息 |
| 平台 -> 设备 | `/p2d/aibox/{deviceSn}` | 平台下发业务请求 |
| 平台 -> 设备 | `/p2d/aibox/heartbeat/{deviceSn}` | 平台心跳相关下发 |

设备连接后会先订阅平台到设备的两个 topic，然后向 `/d2p/aibox` 发送注册消息。注册成功后每 30 秒向 `/d2p/aibox/heartbeat` 发送一次心跳。

## 连接参数

系统配置字段见[字段级 API 参考](api-fields.md#mqtt-参数)。

当前实现支持两种认证模式：

| `authMode` | 行为 |
| --- | --- |
| `0` | 内置 IoT 认证。`clientId` 使用设备 SN，`username` 为 `aibox::{deviceSn}`，`authKey` 为 `cwai` |
| 非 `0` | 普通用户名密码认证。使用配置中的 `clientId`、`userName`、`passwd` |

连接参数中的 `deviceType` 固定为 `box`。

## 外层消息结构

MQTT 外层消息由 `head` 和 `body` 组成：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "request"
  },
  "body": "{\"example\":\"business json string\"}"
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `head.requestId` | string | 请求 ID。响应会复用该值 |
| `head.action` | string | 业务接口路径，例如 `/gtw/cwai/System/QueryDeviceInfo` |
| `head.deviceSn` | string | 设备 SN。设备会拒绝 SN 不匹配的消息 |
| `head.msgType` | string | `register`、`heartbeat`、`request` 或 `response` |
| `body` | string | JSON 字符串。下发请求时需要把业务 JSON `JSON.stringify` 后放入该字段 |

## 注册消息

设备向 `/d2p/aibox` 发送：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "",
    "deviceSn": "DEVICE_SN",
    "msgType": "register"
  },
  "body": {
    "devId": "DEVICE_SN",
    "supplier": "CWAI",
    "aiHostVersion": "v1.0.0",
    "engineType": "cpu",
    "deviceModel": "model",
    "devType": 2
  }
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `body.devId` | string | 设备 ID，当前使用设备 SN |
| `body.supplier` | string | 供应商，默认 `CWAI` |
| `body.aiHostVersion` | string | AI Host 版本 |
| `body.engineType` | string | 引擎类型 |
| `body.deviceModel` | string | 设备型号 |
| `body.devType` | number | 设备类型，当前盒子使用 `2` |

## 心跳消息

设备向 `/d2p/aibox/heartbeat` 发送：

```json
{
  "head": {
    "requestId": "uuid",
    "action": "",
    "deviceSn": "DEVICE_SN",
    "msgType": "heartbeat"
  },
  "body": {
    "devId": "DEVICE_SN",
    "hostStatus": 0,
    "customScore": 1,
    "cpuUsage": 12.3,
    "memTotal": 8192,
    "memAvailable": 4096,
    "gpuUsage": 0,
    "gpuMemTotal": 0,
    "gpuMemAvailable": 0,
    "diskTotal": 128000,
    "diskAvailable": 64000
  }
}
```

心跳发布 ACK 超时时间为 2000 ms。连续失败达到阈值后会触发重连。

## 平台下发请求

平台向 `/p2d/aibox/{deviceSn}` 下发：

```json
{
  "head": {
    "requestId": "REQ-001",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "request"
  },
  "body": "{}"
}
```

设备收到后会：

1. 校验 `deviceSn` 是否等于本机 SN。
2. 校验 `msgType` 是否为 `request`。
3. 使用 `head.action` 作为 API 路由。
4. 将 `body` 字符串作为业务 JSON 分发到 `ApiRouter`。
5. 通过 `/d2p/aibox` 返回响应。

## 设备响应

设备响应外层结构：

```json
{
  "head": {
    "requestId": "REQ-001",
    "action": "/gtw/cwai/System/QueryDeviceInfo",
    "deviceSn": "DEVICE_SN",
    "msgType": "response"
  },
  "body": "{\"resCode\":1,\"resMsg\":[],\"resData\":{}}"
}
```

`body` 是业务响应 JSON 字符串，字段结构与 HTTP API 响应保持一致。
