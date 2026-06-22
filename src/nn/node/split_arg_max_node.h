#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief SplitArgMaxNode
 *
 * SplitArgMaxNode is used to split the input blob into multiple parts.
 * From {b, c} to {b, 2, split.size()}. The split size is specified by split.
 * The top blob`s second dimension is located by (index, value).
 **/
class SplitArgMaxNode : public Node {
public:
    SplitArgMaxNode();

    virtual ~SplitArgMaxNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    std::vector<int> split{};
};

}  // namespace cosmo::nn
