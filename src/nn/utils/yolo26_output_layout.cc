#include "nn/utils/yolo26_output_layout.h"

#include <algorithm>

namespace cosmo::nn {

Yolo26OutputLayout ResolveYolo26OutputLayout(const std::vector<int>& shape, std::optional<bool> raw_output) {
    if (shape.size() != 3 || std::any_of(shape.begin(), shape.end(), [](int dim) { return dim <= 0; }))
        return Yolo26OutputLayout::kInvalid;

    if (raw_output.has_value()) {
        if (*raw_output)
            return shape[1] > 4 ? Yolo26OutputLayout::kRaw : Yolo26OutputLayout::kInvalid;
        return shape[2] == 6 ? Yolo26OutputLayout::kDecoded : Yolo26OutputLayout::kInvalid;
    }
    if (shape[2] == 6)
        return Yolo26OutputLayout::kDecoded;
    return shape[1] > 4 ? Yolo26OutputLayout::kRaw : Yolo26OutputLayout::kInvalid;
}

}  // namespace cosmo::nn
