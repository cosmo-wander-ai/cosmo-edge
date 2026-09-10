# 网络音柱模拟服务

此工具模拟 CosmoEdge 当前接入的网络音柱 HTTP 协议，用于没有真实音柱时验证软件链路：

- 平台对音柱的在线检测；
- 音频文件播放命令及音频 URL 可访问性；
- 文字转语音命令及音量、语速、音色、循环参数；
- 联动策略触发后是否真正向音柱下发命令；
- 音柱返回失败时平台的错误处理。

它验证到“音柱收到并接受正确命令”为止，不模拟功放，也不能代替真实设备的出声、音量和音质验收。

## 启动

模拟服务必须运行在平台设备可访问的测试机上。平台当前只允许填写 IPv4 地址且请求默认使用 80 端口，所以监听端口必须是 80：

```bash
cd /path/to/cosmo-edge
python3 tools/mock_audio_pillar_server.py \
  --host 0.0.0.0 \
  --port 80 \
  --verify-audio-url \
  --log-file /tmp/mock-audio-pillar.jsonl
```

Linux 普通用户若无权监听 80 端口，应由测试机管理员授予该端口的运行权限或使用已有的端口转发；工具本身不要求安装第三方 Python 包。不要把该无认证服务暴露到公网。

先在平台设备所在网络验证可达性，下面的 `TEST_HOST_IP` 替换成运行模拟器的 IPv4 地址：

```bash
curl -sS http://TEST_HOST_IP/v1/check_alive
curl -sS http://TEST_HOST_IP/__mock__/status
```

第一条应返回 `{"code": 200, ...}`，第二条的 `aliveChecks` 应大于 0。

## 平台端到端验收

1. 在“外设管理 → 网络音柱”新增设备，IP 填模拟器所在测试机的 IPv4 地址，网卡选择能到达该测试机的接口。
2. 刷新音柱列表，确认设备在线。访问 `http://TEST_HOST_IP/__mock__/status`，确认 `aliveChecks` 增加。
3. 打开音柱测试，先测试文字播放。确认平台提示成功，状态中的 `speechCommands` 增加，`lastSpeech.kind` 为 `text`，参数与页面填写值一致。
4. 测试音频文件播放。启动参数带有 `--verify-audio-url` 时，模拟器会像真实音柱一样，从平台下发的 URL 拉取一个字节；URL 不可达时返回失败。确认 `lastSpeech.kind` 为 `audio_url` 且 `lastSpeech.audioFetch.ok` 为 `true`。
5. 在联动策略中选择这个模拟音柱，配置算法告警数据和网络音柱联动，保存并触发一次匹配的算法告警。确认 `speechCommands` 再次增加，且 `lastSpeech.payload` 是联动中配置的内容。

查看全部播放命令：

```bash
curl -sS http://TEST_HOST_IP/__mock__/events
```

清空本轮记录后重新测试：

```bash
curl -sS -X DELETE http://TEST_HOST_IP/__mock__/events
```

让下一条播放命令模拟设备内部失败（HTTP 成功但设备业务码为失败）：

```bash
curl -sS -X POST http://TEST_HOST_IP/__mock__/fail-next
```

## 自动化契约测试

在仓库根目录执行：

```bash
python3 test/test_mock_audio_pillar_server.py -v
```

测试会使用随机本地端口，不需要管理员权限，也不会访问真实设备。
