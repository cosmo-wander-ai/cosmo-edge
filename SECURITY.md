# Security Policy

CosmoEdge `v1.1.x` is the current public stable line and receives security fixes. Security maintenance for `v1.0.x` ended when `v1.1.0` was published on 2026-08-25; users should upgrade to `v1.1.x`. Please avoid disclosing security-sensitive details publicly before maintainers have had time to review them. This policy describes how to report vulnerabilities privately and what to expect.

## Reporting a Vulnerability

If you believe you have found a security issue, please report it privately rather than opening a public issue.

Preferred channels, in order of preference:

- **GitHub Private Vulnerability Reporting** — use the "Report a vulnerability" button on the [Security Advisories](https://github.com/cosmo-wander-ai/cosmo-edge/security/advisories/new) page. This is the recommended path; it keeps the report private to maintainers.
- **Email** — if the GitHub channel is unavailable, write to `hello@cosmowander.ai`.

### What to include

Please include the following so we can reproduce and triage quickly:

- affected version or commit
- affected platform, such as x86 Docker or Sophon/aarch64 package
- reproduction steps
- expected impact
- relevant logs or traces with secrets, customer data, device IDs, and private IPs removed

### Response expectations

We follow coordinated disclosure and aim for the following timelines:

- **Acknowledgement** of receipt within 2 business days.
- **Initial assessment** (whether the report is accepted, needs more info, or declined) within 5 business days.
- **A fix or mitigation** in the next regular release, or a dedicated patch release for high-severity issues. We will agree on a public disclosure date with you before publishing.

Status of a report can be requested at any time by replying to the original thread. The status is one of: *Received*, *Triaged*, *In progress*, *Resolved*, or *Declined*.

## Do Not Include

Please do not include sensitive data in public issues, pull requests, screenshots, or logs:

- passwords, tokens, private keys, certificates
- real device SN values
- customer names or project names
- internal domains or private IP addresses
- proprietary model download links
- unredacted production logs
- private images or videos

## Supported Versions

The table below shows the versions that receive security fixes. Builds made directly from `main` between releases are not individually patched; fixes land on `main` and ship in the next release.

| Version | Supported          | Notes                                                                                   |
| ------- | ------------------ | --------------------------------------------------------------------------------------- |
| 1.1.x   | :white_check_mark: | Current public stable line; receives security fixes                                     |
| 1.0.x   | :x:                | Security maintenance ended on 2026-08-25 with `v1.1.0` publication; upgrade to `v1.1.x` |
| 0.1.x   | :x:                | Superseded by v1.0.0; only critical fixes on request                                    |
| < 0.1   | :x:                | Not publicly released                                                                   |

## Deployment Hardening Notes

Before exposing a deployment outside a trusted network, review the items below. The defaults shipped in the repository are tuned for development and evaluation, not for an untrusted network.

### Authentication and credentials

- Change the default `admin` password immediately on first login. The runtime detects a still-default password and warns until it is changed.
- Rotate any tokens, API keys, or certificates before exposing a device. Tokens issued by the web console are UUID-based and expire after a fixed lifetime; do not share browser sessions on shared hosts.
- Treat the auth config file under the configuration directory as sensitive (it stores credentials and active tokens).

### Network exposure

The x86 Docker runtime publishes several ports to the host. Audit each one and restrict ingress accordingly:

| Port (host) | Service                 | Purpose                          |
| ----------- | ----------------------- | -------------------------------- |
| 8080        | nginx (web console)     | Management UI / HTTP API gateway |
| 1936        | SRS                     | RTMP ingest                      |
| 1985        | SRS                     | SRS HTTP API                     |
| 18088       | SRS                     | HTTP streaming                   |
| 8000/udp    | backend                 | Device discovery                 |

The backend HTTP (TCP `8000`) and WebSocket (TCP `9000`) services listen inside the container and are normally reached through the nginx reverse proxy on host port `8080`. Do not publish `8000`/`9000` directly to the host unless you have a specific reason and have protected them.

### General checklist

- web console access control (network ACLs, reverse proxy, IP allow-listing)
- backend HTTP and WebSocket exposure
- SRS API and streaming ports (`1936`, `1985`, `18088`)
- MQTT broker configuration (authentication, TLS, authorization)
- default credentials and tokens
- TLS or reverse-proxy requirements (terminate TLS in front of nginx)
- whether the build is a development-mode build

The x86 Docker developer runtime is intended for development and evaluation, not as a default production security profile. For production field deployments prefer the Sophon BM1688 release package and a `systemd`-managed service behind a hardened network.
