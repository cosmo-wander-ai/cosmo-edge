#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

class DinoDecodeNode : public Node {
public:
    DinoDecodeNode();

    virtual ~DinoDecodeNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetBottomCount() override;
    virtual size_t GetTopCount() override;

    virtual Status InferTopShapes() override;
    virtual bool NeedBottomShapesInfered() override;
    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    float text_threshold;
    float box_threshold;
};

}  // namespace cosmo::nn
