# ONVIF Video Access

ONVIF discovers cameras, reads video Profiles and resolves RTSP addresses. Video uses the existing pull, decode, preview, snapshot and scene-task pipeline. PTZ, audio, event subscriptions and recording search are outside this implementation. This is not an ONVIF certification claim.

## Connect a camera

1. Enable ONVIF on the camera and create a user with video access. Some cameras maintain separate ONVIF and web-management accounts.
2. Add an ONVIF source under Video Input and choose the Manual setup or Auto discovery tab. Separate RTSP credentials are unchecked by default; checking the option reveals their fields.
3. Manual setup: enter a channel name, camera IP address and ONVIF credentials, then click Add channel. The platform queries a stream and saves automatically. Query failures display an error without creating a channel. Ports and complete Device Service paths are also accepted.
4. Auto discovery: before discovery, only the search controls are shown, with no credential fields. The edge device searches all available interfaces and retrieves addresses and channel names. The list shows channel name, read-only IP address, ONVIF username and masked password. Each device has independent credentials prefilled with `admin / admin`; these are input defaults, not discovered credentials. Check and edit each account, select devices and click Add selected devices. Expand a row to enable optional separate RTSP credentials. Neither discovery nor typing sends authentication requests; adding automatically retrieves a stream and saves the channel. Discovery runs on the edge device, not the browser. Use manual entry across routed networks where multicast is unavailable.
5. Use the existing video list to preview, capture snapshots and assign scene tasks. A successful save confirms persistence, not successful video reception.

New channels automatically select a Profile with valid dimensions and H264/H265/MJPEG encoding: highest resolution first, then highest frame rate, preferring H264 when both are equal. Names are not used to infer main/sub streams, and camera encoding settings are not changed. Editing prefers the existing Profile. The simplified UI does not expose Profile selection; the underlying API still accepts explicit Profiles.

Duplicate endpoint/Profile pairs are rejected. Stream and result columns are omitted; failures appear below the corresponding channel name without blocking other devices. Correct that row's credentials and add again to retry. Repeated discovery preserves edited names and credentials. Successfully saved rows are skipped. Row passwords are cleared from memory after successful addition or dialog closure and are not persisted in the browser. Cancel stops pending work but does not abort an in-flight request; dispatched saves retain their success results.

## Maintenance

- Editing changes a channel's name, endpoint or credentials. Empty password fields preserve existing secrets. Saving automatically queries the stream again and causes active video to resolve its address again.
- Authentication failures may indicate a separate ONVIF user, insufficient permissions or clock skew. The client compensates its request timestamp without changing the camera clock.
- If Profile discovery succeeds but video fails, check RTSP permissions, credentials, ports and codec support. SOAP authentication does not prove RTSP authentication.
- For missing discoveries, check edge-device interfaces, UDP 3702 and multicast routing, or use manual entry.
- Requests have bounded timeouts, concurrency limits and retry backoff. Existing scene assignments remain reusable after recovery or credential changes.
- Compare latency using the same Profile, codec, frame rate, GOP and player. ONVIF ultimately uses RTSP and adds no video transcoding pipeline.

Camera configuration stores a logical `onvif://` identifier instead of a password-bearing RTSP URL. Encrypted credentials and their key reside together under the runtime configuration's `onvif/` directory; back up the entire directory. Missing keys must be restored rather than overwriting existing configuration. Encryption does not replace access control; use a trusted network or HTTPS for platform administration.

## Boundaries

Implemented: per-interface IPv4 discovery, manual endpoints, SOAP UsernameToken PasswordDigest and HTTP Digest, Media2 with Media1 fallback. HTTPS certificates are verified; self-signed certificates are not automatically trusted. Advertised SOAP services must remain on the original host. Vendor interoperability requires real-device testing.

Discovery connects to the reply sender's actual IPv4 address while preserving the scheme, port and path of valid advertised service URIs. This handles incorrect hostnames and IPv6-only advertisements without sending credentials to an unrelated advertised host. If no valid URI exists, the default `/onvif/device_service` path is tried; query failures are shown and manual entry remains available. HTTPS certificate verification stays enabled.

Existing video and task APIs remain in use. New authenticated routes are `/gtw/cwai/Camera/OnvifInterfaces`, `OnvifDiscover`, `OnvifProbe`, `OnvifGet` and `OnvifSave`. Clients must check `resData.success` and machine-readable `resData.error`. Do not log credential-bearing request bodies.
