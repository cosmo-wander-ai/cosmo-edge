#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief SumNode.
 *
 */
class SumNode : public Node {
public:
    SumNode();

    virtual ~SumNode();

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;
};

}  // namespace cosmo::nn
