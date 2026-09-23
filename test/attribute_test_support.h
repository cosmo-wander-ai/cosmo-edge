#pragma once

#include "util/AttributeAnalysis.h"

inline cosmo::AttributeSchema TestAttributeSchema() {
    cosmo::AttributeSchema schema;
    schema.attributes = {{"hat",
                          "Hat",
                          "hat-node",
                          "shared-model",
                          "single",
                          0.5,
                          0.6,
                          {{"yes", "yes", "Wearing a hat"}, {"no", "no", "No hat"}}},
                         {"bag",
                          "Bag",
                          "bag-node",
                          "shared-model",
                          "multiple",
                          0.5,
                          0.6,
                          {{"yes", "yes", "Carrying a bag"}, {"no", "no", "No bag"}}}};
    return schema;
}
