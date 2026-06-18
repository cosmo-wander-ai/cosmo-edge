#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief InputNode is a node that is used to input data to the Graph and will do nothing.
 */
class InputNode : public Node {
public:
    InputNode();

    virtual ~InputNode();

    virtual void LoadParam(Op*) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;
};

}  // namespace cosmo::nn
