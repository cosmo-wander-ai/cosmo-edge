#pragma once

#include <memory>
#include <vector>

#include "util/MsgDynamicElement.h"

namespace cosmo {

// -------------- PARAM: Parameters -------------------------
struct ActionParamBase {
    std::string flowActionId{"-1"};
};

void to_json(nlohmann::json& j, const ActionParamBase& v);
void from_json(const nlohmann::json& j, ActionParamBase& v);
using ActionParamBasePtr = std::shared_ptr<ActionParamBase>;

struct ActionParam : public ActionParamBase {
    std::vector<MsgDynamicKeyValue> params;
};

void to_json(nlohmann::json& j, const ActionParam& v);
void from_json(const nlohmann::json& j, ActionParam& v);
using ActionParamPtr = std::shared_ptr<ActionParam>;

struct TaskParam {
    TaskParam(){};
    std::vector<ActionParam> actions;
    friend void to_json(nlohmann::json& j, const TaskParam& v);
    friend void from_json(const nlohmann::json& j, TaskParam& v);
};
using TaskParamPtr = std::shared_ptr<TaskParam>;

}  // namespace cosmo
