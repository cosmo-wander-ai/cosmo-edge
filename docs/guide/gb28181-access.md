# GB28181 设备接入

CosmoEdge 通过随包部署的 SRS 接收国标设备信令和媒体，再把媒体转换为本机 RTMP 实时流。RTMP 从现有 `VideoDemuxer` 进入，因此解码、算法任务、预览、抓拍、录像和告警链路都与 RTSP 通道共用。

```text
GB28181 设备
  └─ SIP/TCP 注册与 INVITE + PS/TCP 媒体
       └─ SRS stream_caster
            └─ rtmp://127.0.0.1:1936/live/<设备ID>
                 └─ 现有解复用、解码、算法与输出链路
```

## 支持范围

当前接入能力面向 GB/T 28181-2016 TCP 设备：

- 设备直接向 CosmoEdge 所在设备注册，SRS 自动发起 INVITE。
- SIP 和媒体均使用 TCP；媒体负载由 SRS 从 PS 转为 RTMP。
- 一个通道使用一个 20 位数字设备 ID，SRS 以设备 ID 作为流名。
- 首期复用实时视频分析能力，不新增独立的国标解码或算法流水线。

当前不包含平台级目录同步、NVR 多子通道选择、PTZ、录像检索/回放和上下级平台级联。需要这些能力时，应在 `IGb28181SourceService` 之外增加独立的国标设备目录与控制服务，媒体输出仍可复用本页的 RTMP 接缝。

## 启用运行配置

SRS 二进制已编译国标能力，但运行时默认关闭监听。通过部署环境设置以下变量，然后按原有方式启动或重启 CosmoEdge：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `COSMO_GB28181_ENABLED` | `off` | 设置为 `on` 启用国标监听 |
| `COSMO_GB28181_CANDIDATE` | `*` | SIP/SDP 中通告给设备的地址；生产环境应显式设置为摄像机可访问的 CosmoEdge LAN IPv4 地址 |
| `COSMO_GB28181_SIP_PORT` | `5060` | SIP/TCP 监听端口 |
| `COSMO_GB28181_MEDIA_PORT` | `9000` | PS/TCP 媒体监听端口 |

示例：

```bash
export COSMO_GB28181_ENABLED=on
export COSMO_GB28181_CANDIDATE=192.168.0.72
export COSMO_GB28181_SIP_PORT=5060
export COSMO_GB28181_MEDIA_PORT=9000
```

启动脚本会校验开关、IPv4 地址和端口范围，再将值渲染到 `${COSMO_DATA_DIR}/runtime/srs.conf`。无效值会阻止启动，避免把任意文本注入 SRS 配置。

确保设备到 CosmoEdge 的 SIP/TCP 端口和 PS/TCP 媒体端口可达。如果设备和 CosmoEdge 跨 NAT，`COSMO_GB28181_CANDIDATE` 必须是设备实际能够访问的地址。

## 配置国标设备

在摄像机或国标设备上配置：

- 协议/传输：GB/T 28181-2016、TCP。
- 服务器地址：CosmoEdge 设备的可访问地址。
- 服务器端口：`COSMO_GB28181_SIP_PORT`，默认 `5060`。
- 设备 ID：20 位数字，并保证同一 CosmoEdge 上唯一。

设备完成注册后，SRS 会自动邀请设备推送媒体。CosmoEdge 内部使用以下地址消费该流：

```text
rtmp://127.0.0.1:1936/live/<20位设备ID>
```

该内部地址不需要也不应手工填写到前端。

## 在 CosmoEdge 新增通道

1. 打开“任务管理”，选择“添加通道”。
2. 接入类型选择 `GB28181`。
3. 填写与设备注册配置一致的 20 位设备 ID。
4. 保存通道并按原流程分配算法任务。

配置文件中保存的是 `gb28181://<设备ID>` 逻辑地址。查询接口和前端不会暴露内部 RTMP 地址；运行时由 `IGb28181SourceService` 解析并交给现有任务通道。

## 在线状态与排查

通道未启动分析任务时，CosmoEdge 通过 SRS HTTP API 检查 `live/<设备ID>` 是否存在活动发布者，而不是只检查本机端口。通道开始读取后，继续沿用现有解复用状态判断在线、异常和结束状态。

可在设备上只读检查：

```bash
ss -ltn | grep -E ':(5060|9000) '
curl -s 'http://127.0.0.1:1985/api/v1/streams/?start=0&count=1000'
```

重点确认：

- `${COSMO_DATA_DIR}/log/logs/srs.log` 中没有 SIP 注册、INVITE 或媒体接收错误。
- Streams API 中 `name` 等于设备 ID、`app` 为 `live`，并且 `publish.active` 为 `true`。
- 设备填写的服务器地址和端口与渲染后的 `srs.conf` 一致。
- H.264 是首选验证编码；H.265 是否可用还取决于设备的国标封装和目标算能平台解码能力，应以目标设备实测为准。

SRS 国标实现与配置语义可参考 [SRS GB28181 文档](https://ossrs.io/lts/zh-cn/docs/v5/doc/gb28181)。
