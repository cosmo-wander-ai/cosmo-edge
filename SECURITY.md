# Security Policy

CosmoEdge is being prepared for public open-source release. Please avoid disclosing security-sensitive details publicly before maintainers have had time to review them.

## Reporting a Vulnerability

If you believe you have found a security issue, please report it privately.

Preferred contact:

- Email: hello@cosmowander.ai

Please include:

- affected version or commit
- affected platform, such as x86 Docker or Sophon/aarch64 package
- reproduction steps
- expected impact
- relevant logs or traces with secrets, customer data, device IDs, and private IPs removed

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

The first public release process is still being prepared. Supported versions will be listed here after the initial public release.

## Deployment Notes

Before exposing a deployment outside a trusted network, review:

- web console access control
- backend HTTP and WebSocket exposure
- SRS API and streaming ports
- MQTT broker configuration
- default credentials and tokens
- TLS or reverse-proxy requirements
- whether the build is a development-mode build

The x86 Docker developer runtime is intended for development and evaluation, not as a default production security profile.
