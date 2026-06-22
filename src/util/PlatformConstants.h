// PlatformConstants.h — Compile-time platform-specific constants.
//
// Provides directory naming prefixes, engine type identifiers, and model file
// extensions that differ between Sophon (BM1688) and CPU (x86) backends.
// All values are constexpr so they resolve at compile time with zero runtime cost.
//
// Usage:
//   #include "util/PlatformConstants.h"
//   std::string dir = cosmo::util::kPlatformDirPrefix + modelCode + "_" + name;

#pragma once

namespace cosmo::util {

#ifdef COSMO_NN_USE_SOPHON_BACKEND

/// Directory prefix for Sophon backend model directories: "prod_BM1688_"
static constexpr const char* kPlatformDirPrefix = "prod_BM1688_";

/// Regex pattern to extract algorithm code from Sophon model directory names.
static constexpr const char* kPlatformDirRegex = "prod_BM1688_([0-9]+)_.*";

/// Engine type identifier reported to frontend / device info API.
static constexpr const char* kEngineType = "BM1688";

/// Model binary file extension for Sophon backend (.nn wraps .bmodel).
static constexpr const char* kModelFileExt = ".nn";

#elif defined(COSMO_NN_USE_CPU_BACKEND)

/// Directory prefix for CPU/x86 backend model directories: "prod_X86_"
static constexpr const char* kPlatformDirPrefix = "prod_X86_";

/// Regex pattern to extract algorithm code from x86 model directory names.
static constexpr const char* kPlatformDirRegex = "prod_X86_([0-9]+)_.*";

/// Engine type identifier reported to frontend / device info API.
static constexpr const char* kEngineType = "X86";

/// Model binary file extension for CPU backend (.onnx used directly).
static constexpr const char* kModelFileExt = ".onnx";

#else
#error "Either COSMO_NN_USE_SOPHON_BACKEND or COSMO_NN_USE_CPU_BACKEND must be defined"
#endif

}  // namespace cosmo::util
