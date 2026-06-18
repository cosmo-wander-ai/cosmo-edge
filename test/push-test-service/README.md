# AI Box 推送测试服务

这个小服务用于联调平台对外推送能力：

- HTTP：接收事件推送，打印关键字段，并返回平台期望的 JSON。
- MQTT：内置一个本地 MQTT broker，接收设备注册、心跳、业务消息，并按协议自动回包。

## 启动

```bash
cd scripts/push-test-service
npm install
npm start
```

默认监听：

- HTTP: `http://127.0.0.1:18080/events`
- MQTT: `127.0.0.1:1883`

可以通过环境变量调整：

```bash
HTTP_PORT=18080 MQTT_PORT=1883 npm start
```

Windows PowerShell:

```powershell
$env:HTTP_PORT="18080"; $env:MQTT_PORT="1883"; npm start
```

## 平台侧配置建议

HTTP 推送地址配置为：

```text
http://测试机IP:18080/events
```

MQTT 地址配置为：

```text
测试机IP:1883
```

测试服务会关注这些 MQTT topic：

- `/d2p/aibox`
- `/d2p/aibox/heartbeat`
- `/p2d/aibox/{sn}`
- `/p2d/aibox/heartbeat/{sn}`

## 常用验证

HTTP 手工发送一条事件：

```bash
curl -X POST http://127.0.0.1:18080/events \
  -H "Content-Type: application/json" \
  -d "{\"messageId\":\"msg-1\",\"recordId\":\"rec-1\",\"devId\":\"box-1\",\"algorithmCode\":\"face\",\"property\":{\"face\":{\"confidence\":0.98}}}"
```

服务正常时返回：

```json
{"resCode":1,"resMsg":[]}
```
