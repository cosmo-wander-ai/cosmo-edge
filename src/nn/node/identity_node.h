#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief IdentityNode is a pass-through node that simply copies input to output.
 * Used for decoder inputs without preprocessing (e.g., point_labels, mask_input, has_mask_input).
 * This node acts as a first_calculate_node to convert params to blobs.
 */
class IdentityNode : public Node {
public:
    IdentityNode();
    virtual ~IdentityNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetBottomCount() override;
    virtual size_t GetTopCount() override;

    virtual Status InferTopShapes() override;
    virtual bool NeedBottomShapesInfered() override;
    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    // first_calculate_node Forward (receives params from user)
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    // Set expected shape and data type for this identity node
    void SetExpectedShape(const DimsVector& shape, DataType data_type);

private:
    DimsVector expected_shape;
    DataType expected_data_type;
};

}  // namespace cosmo::nn
