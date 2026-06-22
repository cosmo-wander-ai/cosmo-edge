#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

class SAMDecodeNode : public Node {
public:
    SAMDecodeNode();
    virtual ~SAMDecodeNode();

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
    float threshold;
    std::vector<int> output_size;  // [height, width] for mask resizing

    // Helper function to resize mask
    void ResizeMask(float* input_mask, int in_h, int in_w, uint8_t* output_mask, int out_h, int out_w);
};

}  // namespace cosmo::nn
