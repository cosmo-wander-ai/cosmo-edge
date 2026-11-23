#pragma once

#include <cmath>
#include <vector>

#include "flow/detect/PersonTemplateContract.h"
#include "service/model/dto/ModelDto.h"

namespace cosmo::person_template_contract {
// Metadata visible to the user and later consumed by camera parameter setup
// must describe the one-class contract. A duplicate user model with different
// labels must not silently replace this model in the picker.
inline const Model::MsgAtomicModel* CompatibleMetadata(const std::vector<Model::MsgAtomicModel>& models,
                                                       float minimum_confidence) {
    for (const auto& model : models) {
        if (model.atomicCode != "1001003")
            continue;
        if (model.modelType != "axera_native_person" || model.labelList.size() != 1)
            return nullptr;
        const auto& label = model.labelList.front();
        if (label.label != "0" || label.class_name != "pedestrian" ||
            !ValidThreshold(label.threshold, minimum_confidence))
            return nullptr;
        return &model;
    }
    return nullptr;
}
}  // namespace cosmo::person_template_contract
