#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nn/core/status.h"

namespace cosmo::nn {
class Blob;

class LayaRunner {
public:
    LayaRunner();
    ~LayaRunner();
    Status Init(const std::string& model_path, const std::string& tokenizer_path, int device_id);
    Status Run(const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs);
    Status PrepareText(const std::string& input, std::string& output);
    const std::vector<std::vector<std::string>>& Outputs() const {
        return outputs_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::vector<std::string>> outputs_;
};
}  // namespace cosmo::nn
