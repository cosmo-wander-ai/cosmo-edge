# Cosmo Model Guard v2.6.0 RKNN ABI 1 candidate SDK

This SDK contains the unified RK3576/RK3588 Model Guard runtime, public header, and device
provisioning tool. It contains no product model key, signing seed, device
certificate, or protected-model plaintext.

This candidate may be checked into the explicitly authorized CosmoEdge fork to
make its RK3576 build reproducible. That task-scoped authorization is not a
general relicensing or permission to publish the Guard source, trust inputs, or
SDK elsewhere. Upstream or public distribution still requires a separate
review and an approved immutable runtime hash.

The local device-tree identity selects profile 2 / OTP offset 0x0a on RK3576
and profile 4 / OTP offset 0x07 on RK3588. Certificate storage is caller-selected.
The four CMGR_1.0 exports, SONAME 1 and public structures remain compatible.
Each chip requires a model converted and encrypted for its own target; an
existing RK3576 model or certificate does not authorize an RK3588 device.

Dependencies: Linux AArch64, glibc >= 2.31, a compatible libstdc++, OpenSSL 3
libcrypto.so.3 and an RKNN runtime compatible with both the deployed driver
and the published model. The SDK does not include or replace those libraries.
The pinned builder uses RKNN 2.3.2. Application owners must validate their
actual runtime/driver pair. The offline publisher is distributed separately.
