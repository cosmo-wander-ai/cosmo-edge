// VehicleDict — Vehicle Dict implementation.

#include "util/VehicleDict.h"

#include "util/AiTypes.h"

namespace cosmo {

namespace {

    // Helper to build a key-value entry concisely
    MsgDynamicKeyValue MakeKV(const std::string& key, const std::string& value) {
        MsgDynamicKeyValue el;
        el.key   = key;
        el.value = value;
        return el;
    }

    MsgDynamicKeyValue MakeKV(const std::string& key, int value) {
        return MakeKV(key, std::to_string(value));
    }

}  // namespace

std::vector<MsgDynamicKeyValue> DictVehicleColor() {
    return {
        MakeKV("黑色", static_cast<int>(GBVehicleColorType::kBlack)),
        MakeKV("白色", static_cast<int>(GBVehicleColorType::kWhite)),
        MakeKV("灰色", static_cast<int>(GBVehicleColorType::kGray)),
        MakeKV("红色", static_cast<int>(GBVehicleColorType::kRed)),
        MakeKV("蓝色", static_cast<int>(GBVehicleColorType::kBlue)),
        MakeKV("黄色", static_cast<int>(GBVehicleColorType::kYellow)),
        MakeKV("橙色", static_cast<int>(GBVehicleColorType::kOrange)),
        MakeKV("棕色", static_cast<int>(GBVehicleColorType::kBrown)),
        MakeKV("绿色", static_cast<int>(GBVehicleColorType::kGreen)),
        MakeKV("紫色", static_cast<int>(GBVehicleColorType::kPurple)),
        MakeKV("青色", static_cast<int>(GBVehicleColorType::kCyan)),
        MakeKV("粉色", static_cast<int>(GBVehicleColorType::kPink)),
        MakeKV("混色", static_cast<int>(GBVehicleColorType::kMixColor)),
        MakeKV("多辆车", static_cast<int>(GBVehicleColorType::kMultiCar)),
        MakeKV("无法判断", static_cast<int>(GBVehicleColorType::kColorPoorQuality)),
        MakeKV("其他", static_cast<int>(GBVehicleColorType::kOther)),
    };
}

std::vector<MsgDynamicKeyValue> DictVehiclePlateColor() {
    return {
        MakeKV("蓝色", "5"), MakeKV("绿色", "9"), MakeKV("黄色", "6"),
        MakeKV("白色", "2"), MakeKV("黑色", "1"),
    };
}

std::vector<MsgDynamicKeyValue> DictVehicleClass() {
    return {
        MakeKV("轿车", "K33"),         MakeKV("越野车", "K32"),       MakeKV("客车", "K20"),
        MakeKV("面包车", "K40"),       MakeKV("货车", "H"),           MakeKV("专项作业车", "Z"),
        MakeKV("运煤车类型1", "Z501"), MakeKV("运煤车类型2", "Z502"), MakeKV("运煤车类型3", "Z503"),
        MakeKV("电动三轮车", "N"),     MakeKV("摩托车", "M"),         MakeKV("其他", "X"),
    };
}

std::vector<MsgDynamicKeyValue> DictVehicleOrientation() {
    return {
        MakeKV("朝前", "1"),
        MakeKV("朝后", "2"),
        MakeKV("侧面", "3"),
        MakeKV("无法判断", "9"),
    };
}

}  // namespace cosmo
