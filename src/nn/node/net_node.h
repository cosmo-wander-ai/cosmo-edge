#pragma once

#include "nn/node/node.h"
#include "nn/utils/model_info_utils.h"

namespace cosmo::nn {

/**
 * @brief NetNode is a node that contains a network
 */
class NetNode : public Node {
public:
    NetNode();

    virtual ~NetNode();

    virtual void LoadParam(Op*) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual Status LoadWeight(const char* data, size_t size) = 0;

    void SetNetworkInputNames(std::vector<std::string> names);

    void SetNetworkOutputNames(std::vector<std::string> names);

    void SetOutputToCpu(bool v) {
        output_to_cpu_ = v;
    }

protected:
    bool output_to_cpu_ = false;
    size_t bottom_count = 0;
    size_t top_count    = 0;

    // network actual input/output names
    std::vector<std::string> network_input_names{};
    std::vector<std::string> network_output_names{};
};

}  // namespace cosmo::nn
