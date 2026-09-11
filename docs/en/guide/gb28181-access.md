# GB28181 Device Access

Registration and video-channel invitation are separate operations. A video channel ID does not replace SIP server, domain, identity or authentication settings. This branch is an initial managed-access trial, not full GB28181 certification or universal vendor acceptance.

## Architecture and Scope

CosmoEdge owns SIP/TCP registration, Digest MD5 authentication, heartbeats, Catalog and INVITE/ACK/BYE. SRS receives PS/TCP and converts it to `rtmp://127.0.0.1:1936/live/<video-channel-id>`. Existing demux, decode, algorithm, preview, snapshot, recording and alarm paths are reused.

Device IDs and video channel IDs are separate. Multi-message catalogs are published only when complete. Duplicate channel IDs across devices are reported, not ambiguously matched. Existing `gb28181://<20-digit-id>` addresses remain valid when their IDs appear in a registered device catalog.

Current limits:

- TCP signaling and camera-initiated TCP media only; the platform receives media passively. This is a bounded SIP subset, not a general SIP proxy.
- At most 128 registrations, 64 concurrent SIP connections, 1,024 entries per device catalog and 256 media demands. Existing channel/decoder limits still apply.
- H.264 is preferred for validation. H.265 needs camera-packaging, target-hardware and browser verification.
- No UDP, platform-initiated media TCP, SIP TLS, SHA-256 Digest, Record-Route/proxy/cascading, catalog subscriptions, PTZ, audio business features or GB recording search/playback. Missing catalogs do not trigger guessed channels.
- NVR identity separation and pagination have simulated coverage, not cross-vendor device acceptance.

## Camera-to-Platform Mapping

Click **Add** in Video Channels and select GB28181 to open **GB28181 access**. The dialog contains only the Platform and Devices & channels tabs.

| Camera field | CosmoEdge setting | Meaning |
| --- | --- | --- |
| SIP server ID | Platform → SIP server ID | Common platform identity |
| SIP server domain | Platform → SIP server domain | Also the Digest realm; must match |
| SIP server address | Reachable platform address | Not the camera's own address |
| SIP server port | Platform → SIP server port | TCP 5060 by default |
| SIP username | Devices → Device ID | Device's 20-digit identity |
| SIP authentication ID | Advanced authentication | Defaults to device ID; may differ |
| Password | Device SIP password | Must match; not automatically taken from web login, ONVIF or platform admin |
| Video channel ID | Automatically discovered catalog | May differ from device ID; an NVR can expose several |
| Transport | TCP | Camera initiates media connection |

New registrations require a password. A blank password on edit preserves the saved secret. Digests are stored privately, encrypted and atomically; queries return neither passwords nor digests. Changing the realm requires recreating device registrations and their passwords.

The explicit per-device **skip password verification** option is disabled by default and intended only for trusted isolated networks. An ID match does not authenticate a physical device. Unlisted IDs remain blocked. HTTP management and SIP/TCP do not provide transport encryption; do not expose them directly to the Internet.

## Setup

1. Confirm platform ID, domain and SIP port, enable access and save. SIP server address defaults to Automatic, using the local IPv4 interface reached by each camera. Select a fixed interface IP from the dropdown, or enter a reachable address under Advanced address settings for NAT or special routing. Cameras still need a reachable box IP; automatic mode does not save the browser login address as a fixed IP.
2. Register the device ID and SIP password. Override its authentication ID in Advanced authentication if needed.
3. Configure matching fields on the camera, enable GB28181 and select TCP.
4. Wait for registration and automatic catalog discovery. Expand the device, edit desired channel names and add them. Playback URLs are not required; catalog entries are not imported indiscriminately.
5. Preview in Video Input and assign existing tasks. Existing matching logical sources are marked Added.

Platform changes disconnect GB registrations and require re-registration. Credential edits require that device to register again. Removing a registration preserves business channels and tasks but takes them offline.

## Runtime and Migration

The SRS media caster is enabled while its built-in SIP listener is disabled. CosmoEdge's managed SIP listener defaults to disabled and is explicitly enabled in the UI.

Legacy `COSMO_GB28181_ENABLED`, `COSMO_GB28181_SIP_PORT` and `COSMO_GB28181_CANDIDATE` no longer control managed SIP; saved UI settings are authoritative. `COSMO_GB28181_MEDIA_PORT` still controls SRS media reception, TCP 9001 by default. Avoid WebSocket port 9000. Both SIP and media ports must be reachable.

Media allocation/release accepts only local-loopback POST requests; public RTC APIs are unchanged. Unallocated media does not create business channels.

Configuration is under `${COSMO_DATA_DIR}/conf/gb28181/`. Back up and restore `key` and `settings.json` together with business channel configuration. Missing keys, corruption or decryption failures never fall back to unauthenticated registration.

For upgrades, preserve existing channels, register actual device credentials and query the catalog. The logical ID must identify a video channel, not necessarily its parent device.

## Diagnostics

| State | Check first |
| --- | --- |
| Waiting / inactive SIP listener | Enable switch, port conflict, firewall, server address and device ID |
| Authentication failed | SIP authentication ID, realm and SIP password |
| Catalog failed | Catalog support, complete responses and manual retry |
| Inviting / rejected | Video channel ID, SIP status and transport support |
| Waiting for media / timeout | Advertised address, media port and camera-active TCP support |
| Receiving | Active publisher only; verify decoded picture and tasks separately |

Registration/heartbeat expiry takes a device offline; re-registration refreshes the catalog. Failed invitations time out and retry. Removed business channels lose media demand after approximately 120 seconds; active algorithm channels also renew demand. SRS independently expires unstarted or inactive media sessions. These are recovery mechanisms, not instant-reconnect guarantees.

SRS interleaving is bounded to approximately 200 ms of timestamp span or 32 messages, so video-only streams or missing audio no longer wait for over 100 video packets. This is not an end-to-end latency bound: the camera, keyframes, network, decoder and player still buffer data. Compare RTSP and GB28181 using the same stream, codec, resolution, frame rate, keyframe interval and preview method.

The RTMP input strategy also uses a shorter stream probe (500 ms analysis duration, 1 MiB probe size and five-frame FPS probing) so low-frame-rate video does not exhaust the preview startup deadline. This does not change RTSP/ONVIF inputs or guarantee a fixed first-picture time.

References: [SRS 6 External SIP](https://ossrs.io/lts/zh-cn/docs/v6/doc/gb28181#external-sip), [SIP RFC 3261](https://www.rfc-editor.org/rfc/rfc3261). Session release/expiry and local-control restrictions are applied to the build copy, not `3rd/`. Upstream capabilities are not device-acceptance evidence for this branch.
