#pragma once

#include <string>
#include <vector>

#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo {

// Orders a single-parent forest without relying on the serialized array order.
// Rejects cycles, duplicate identities and missing parents; never changes input on failure.
bool OrderWorkflow(std::vector<ActionNode>& nodes, std::string& error);
bool ValidatePictureWorkflow(std::vector<ActionNode>& nodes, std::string& error);
bool ValidatePictureParams(const std::vector<MsgDynamicKeyValue>& params);
bool IsPictureModelAction(const std::string& id);
bool IsPictureAction(const std::string& id);

}  // namespace cosmo
