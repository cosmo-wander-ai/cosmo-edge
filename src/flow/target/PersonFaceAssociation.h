#pragma once

#include <vector>

#include "infer/AiCommon.h"

namespace cosmo {

// Associate only unambiguous, one-to-one faces from the same frame. People and
// their tracking/area metadata survive even when they have no usable face.
void AssociatePersonFaces(std::vector<AiDetectRstEl>& people, const std::vector<AiDetectRstEl>& faces,
                          int min_face_size);

}  // namespace cosmo
