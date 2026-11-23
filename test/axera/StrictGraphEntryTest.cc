#include <cassert>
#include <cstdlib>

#include "nn/core/blob_store.h"
#include "nn/core/shared_resource.h"
#include "nn/node/node.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/profiler.h"
#define private public
#include "nn/core/graph.h"
#undef private
#include "nn/node/node_type_utils.h"

namespace cosmo::nn {
bool NodeTypeUtils::IsInputNode(Node*) {
    return false;
}
}  // namespace cosmo::nn
struct CountingNode final : cosmo::nn::Node {
    explicit CountingNode(int& calls) : calls_(calls) {
        first_calculate_node = true;
    }
    cosmo::nn::Status Forward(std::vector<std::shared_ptr<cosmo::nn::Blob>>&,
                              std::vector<std::shared_ptr<cosmo::nn::Blob>>&) override {
        ++calls_;
        return cosmo::nn::COSMO_NN_OK;
    }
    int& calls_;
};
int main() {
    using namespace cosmo::nn;
    unsetenv("COSMO_AXERA_STRICT_ZERO_COPY");
    Graph graph;
    graph.device_type_ = DEVICE_AXERA;
    int node_calls = 0, provider_calls = 0;
    graph.nodes.emplace_back(std::make_unique<CountingNode>(node_calls));
    BlobDesc desc;
    BlobHandle handle;
    handle.host_data_provider = [&]() -> void* {
        ++provider_calls;
        return nullptr;
    };
    auto input = std::make_shared<Blob>(desc, handle);
    std::vector<std::vector<std::shared_ptr<Blob>>> params{{input}};
    assert(!graph.Forward(params));
    assert(node_calls == 0 && provider_calls == 0);
}
