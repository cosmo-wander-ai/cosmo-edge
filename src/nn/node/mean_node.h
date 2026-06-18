#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

class MeanNode : public Node {
public:
    MeanNode();

    virtual ~MeanNode();

    virtual size_t GetBottomCount() override;
    virtual size_t GetTopCount() override;

    virtual bool NeedBottomShapesInfered() override;

    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
};

}  // namespace cosmo::nn
