---
title: 后端开发
description: 当前 C++ 后端构建、模块、测试和编码约定入口。
prev:
  text: 前端工程
  link: /development/frontend
---

# 后端开发

后端主要使用 C++17 和 CMake。

## 构建入口

| 文件 | 说明 |
| --- | --- |
| `CMakeLists.txt` | 顶层构建和打包入口 |
| `scripts/build_cpu.sh` | x86 CPU 后端构建 |
| `scripts/build.sh` | Sophon/aarch64 构建 |
| `scripts/build_cpu_test.sh` | CPU 测试构建 |

## CMake 关键选项

| 选项 | 说明 |
| --- | --- |
| `COSMO_TARGET_ARCH` | `aarch64` 或 `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | 启用 Sophon 后端 |
| `COSMO_NN_USE_CPU_BACKEND` | 启用 CPU/ONNX Runtime 后端 |
| `COSMO_ENABLE_OPENH264` | CPU 后端启用 OpenH264 |
| `COSMO_ENABLE_GPL_CODECS` | 启用 GPL codec，发布时需谨慎 |
| `COSMO_DEV_MODE` | 开发模式，跳过部分生产校验 |
| `BUILD_TESTS` | 构建测试目标 |

## 测试

```bash
bash scripts/build_cpu_test.sh
```

该脚本构建：

```text
cosmo-tests
```

测试源码位于：

```text
test/
```

## 代码风格

仓库包含：

- `.clang-format`
- `.clang-tidy`
- `.cppcheck-suppressions`
- `CODING_STYLE.md`

贡献代码前应尽量保持现有风格和模块边界，不在无关改动中做大规模重构。

## 服务开发入口

新增或修改业务能力时，通常需要关注：

- `src/service` 中的接口和实现。
- `src/app/app_init.cc` 中的服务注册和初始化顺序。
- `src/api` 中的路由和消息处理。
- `src/flow` 中的任务/算法链路。
- `data/resource/*` 中的前端配置和模板资源。
