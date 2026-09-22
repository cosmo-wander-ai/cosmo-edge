#pragma once

#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace cosmo::infer_detail {

// An empty outer result is missing inference output, never "zero targets". A successful
// zero-target frame has its own empty inner vector. Reject before touching prior batches.
template <typename Target>
bool AppendCompleteDetectorBatch(size_t inputCount, std::vector<std::vector<Target>>&& batch,
                                 std::vector<std::vector<Target>>& results) {
    if (inputCount == 0 || batch.size() != inputCount) {
        return false;
    }
    results.insert(results.end(), std::make_move_iterator(batch.begin()),
                   std::make_move_iterator(batch.end()));
    return true;
}

}  // namespace cosmo::infer_detail
