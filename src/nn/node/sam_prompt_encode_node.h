#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

class SAMPromptEncodeNode : public Node {
public:
    SAMPromptEncodeNode();
    virtual ~SAMPromptEncodeNode();

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
    std::string prompt_type;  // "point" or "box"
    bool normalize;
    int encoder_size;  // encoder input size (e.g., 1024)
    int max_points;    // maximum number of points for padding

    // Helper functions
    void NormalizeCoords(float* coords, int num_points, int orig_width, int orig_height);
    void PadPoints(float* input_coords, int input_num, float* output_coords, int output_num);
};

}  // namespace cosmo::nn
