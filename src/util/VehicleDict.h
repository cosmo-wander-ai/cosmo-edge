#pragma once

#include <vector>

#include "util/MsgDynamicElement.h"

namespace cosmo {

std::vector<MsgDynamicKeyValue> DictVehicleColor();
std::vector<MsgDynamicKeyValue> DictVehiclePlateColor();
std::vector<MsgDynamicKeyValue> DictVehicleClass();
std::vector<MsgDynamicKeyValue> DictVehicleOrientation();  // fixed typo: was "Rientation"

}  // namespace cosmo
