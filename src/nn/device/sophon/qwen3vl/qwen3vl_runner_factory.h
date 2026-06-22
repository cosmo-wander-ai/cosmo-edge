#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nn/core/status.h"

namespace cosmo::nn {

class Blob;

/**
 * Opaque interface: default_component.cc interacts with Qwen3VLRunner only through this header,
 * without including the full Qwen3VLRunner definition, avoiding unique_ptr<Impl> destructor
 * instantiation in other TUs.
 */
class Qwen3VLRunner;  // Forward declaration only

Qwen3VLRunner* Qwen3VLRunner_Create();
void Qwen3VLRunner_Destroy(Qwen3VLRunner* r);

Status Qwen3VLRunner_Init(Qwen3VLRunner* r, const std::string& model_path, const std::string& tokenizer_path,
                          int device_id, const std::string& model_config_json_path,
                          const std::string& model_type = "qwen3vl");

Status Qwen3VLRunner_Run(Qwen3VLRunner* r, const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs);

const std::vector<std::vector<std::string>>& Qwen3VLRunner_GetTextOutputs(const Qwen3VLRunner* r);

int Qwen3VLRunner_GetMaxBatchSize(const Qwen3VLRunner* r);

}  // namespace cosmo::nn
