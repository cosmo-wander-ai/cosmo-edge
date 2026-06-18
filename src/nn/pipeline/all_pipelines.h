#pragma once

// Include all pipeline implementations to ensure static registration
// Include this header in at least one .cc file to trigger auto-registration

#include "nn/pipeline/model_pipeline.h"

// Concrete pipeline headers (registration happens in their .cc files)
#include "advanced_pipeline.h"
#include "classify_pipeline.h"
#include "detection_pipeline.h"
#include "feature_pipeline.h"
#include "keypoints_pipeline.h"
