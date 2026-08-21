# GB28181 Device Access

CosmoEdge uses its packaged SRS process to receive GB28181 signaling and media, then converts the media into a local RTMP live stream. RTMP enters the existing `VideoDemuxer`, so decoding, algorithm tasks, preview, snapshots, recording, and alarms reuse the RTSP channel pipeline.

```text
GB28181 device
  └─ SIP/TCP registration and INVITE + PS/TCP media
       └─ SRS stream_caster
            └─ rtmp://127.0.0.1:1936/live/<device-id>
                 └─ existing demux, decode, algorithm, and output pipeline
```

## Supported Scope

The current adapter targets GB/T 28181-2016 devices using TCP:

- The device registers directly with the CosmoEdge host and SRS sends the INVITE automatically.
- SIP and media use TCP; SRS converts the PS media payload to RTMP.
- Each channel uses one unique 20-digit device ID, which SRS also uses as the stream name.
- The first release reuses live video analytics and does not introduce a separate GB28181 decode or algorithm pipeline.

Platform-level catalog synchronization, NVR sub-channel selection, PTZ, recording search/playback, and upstream/downstream platform cascading are not included. Add a separate GB28181 catalog and control service next to `IGb28181SourceService` when those capabilities are required; its media output can still reuse the RTMP seam described here.

## Enable the Runtime Configuration

The SRS binary is built with GB28181 support, while the listeners are disabled by default at runtime. Set these variables in the deployment environment, then start or restart CosmoEdge in the usual way:

| Variable | Default | Description |
| --- | --- | --- |
| `COSMO_GB28181_ENABLED` | `off` | Set to `on` to enable GB28181 listeners |
| `COSMO_GB28181_CANDIDATE` | `*` | Address advertised through SIP/SDP; production deployments should explicitly set the CosmoEdge LAN IPv4 address reachable by the camera |
| `COSMO_GB28181_SIP_PORT` | `5060` | SIP/TCP listener |
| `COSMO_GB28181_MEDIA_PORT` | `9000` | PS/TCP media listener |

Example:

```bash
export COSMO_GB28181_ENABLED=on
export COSMO_GB28181_CANDIDATE=192.168.0.72
export COSMO_GB28181_SIP_PORT=5060
export COSMO_GB28181_MEDIA_PORT=9000
```

The startup scripts validate the switch, IPv4 address, and port ranges before rendering `${COSMO_DATA_DIR}/runtime/srs.conf`. Invalid values stop startup so arbitrary text cannot be injected into the SRS configuration.

Allow the device to reach both the SIP/TCP and PS/TCP media ports. Across NAT, `COSMO_GB28181_CANDIDATE` must be an address that the device can actually reach.

## Configure the GB28181 Device

Configure the camera or GB28181 device with:

- Protocol/transport: GB/T 28181-2016 over TCP.
- Server address: the reachable address of the CosmoEdge device.
- Server port: `COSMO_GB28181_SIP_PORT`, default `5060`.
- Device ID: exactly 20 digits and unique within this CosmoEdge instance.

After registration, SRS automatically invites the device to publish media. CosmoEdge consumes it internally at:

```text
rtmp://127.0.0.1:1936/live/<20-digit-device-id>
```

Do not enter this internal address in the UI.

## Add the Channel in CosmoEdge

1. Open Task Management and select Add Channel.
2. Select `GB28181` as the access type.
3. Enter the same 20-digit device ID used in the registration configuration.
4. Save the channel and assign algorithm tasks through the existing workflow.

The persisted configuration contains the logical address `gb28181://<device-id>`. Query APIs and the UI do not expose the internal RTMP address; `IGb28181SourceService` resolves it for the existing task channel at runtime.

## Online Status and Troubleshooting

When analysis is stopped, CosmoEdge queries the SRS HTTP API for an active publisher named `live/<device-id>` instead of merely probing a local TCP port. Once the channel is reading, the existing demux states continue to drive online, abnormal, and end-of-stream status.

Read-only checks on the device:

```bash
ss -ltn | grep -E ':(5060|9000) '
curl -s 'http://127.0.0.1:1985/api/v1/streams/?start=0&count=1000'
```

Verify that:

- `${COSMO_DATA_DIR}/log/logs/srs.log` has no SIP registration, INVITE, or media receive errors.
- The Streams API reports `name` equal to the device ID, `app` equal to `live`, and `publish.active` equal to `true`.
- The device server address and ports match the rendered `srs.conf`.
- H.264 is the preferred validation codec. H.265 also depends on the device's GB28181 packaging and the target Sophon decoder, so verify it on the target device.

See the [SRS GB28181 documentation](https://ossrs.net/lts/en-us/docs/v5/doc/gb28181) for the underlying SRS protocol and configuration behavior.
