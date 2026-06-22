#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

class SplitNode : public Node {
public:
    SplitNode();

    virtual ~SplitNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual bool NeedBottomShapesInfered() override;

    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    int axis = 0;
    std::vector<int> split;
};

}  // namespace cosmo::nn
