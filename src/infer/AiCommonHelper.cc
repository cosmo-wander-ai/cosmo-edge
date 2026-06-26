// AiCommonHelper.cc — Helper utilities for AiCommon.
// Split from AiCommon.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <codecvt>
#include <locale>
#include <regex>
#include <stdexcept>
#include <unordered_map>

#include "infer/AiCommon.h"
#include "util/Log.h"

namespace cosmo {

bool areaIdInAreaUnits(const std::vector<TargetAreaUnit>& areas, const std::string& area) {
    return std::any_of(areas.begin(), areas.end(),
                       [&area](const auto& areaUnit) { return areaUnit.area_id == area; });
}

// License plate related
// Predefined character replacement table (fixes common OCR misrecognitions)
const std::unordered_map<char, char> kCharReplacement{
    {'O', '0'},
    {'Q', '0'},
    /*{'D','0'},*/  // Easily confused digits
    {'I', '1'},
    {'L', '1'},
    {'T', '1'},
    /*{'Z','2'},*/ {'S', '5'},
    {'B', '8'} /*,{'U','V'}, {'M','N'}*/  // Easily confused letters
};

// License plate regex rules (based on Chinese plate standards)
const std::vector<std::wregex> kPlateRules{
    // Standard civilian plate (e.g. 浙A12345)
    {std::wregex(
        L"^[京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼使领]{1}[A-HJ-NP-Z]{1}[A-HJ-NP-"
        L"Z0-9]{4,5}[A-HJ-NP-Z0-9挂学警港澳]{1}$")},

    // New energy vehicle plate (e.g. 浙AD12345 or 浙A12345D)
    {std::wregex(
        L"^[京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼使领]{1}[A-HJ-NP-Z]{1}([DF]{1}["
        L"A-HJ-NP-Z0-9]{5}|[0-9]{5}[DF]{1})$")},

    // Police plate (e.g. 浙A1234警)
    {std::wregex(
        L"^[京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼]{1}[A-HJ-NP-Z]{1}[A-HJ-NP-Z0-"
        L"9]{4}警$")},

    // Embassy plate (e.g. 使A12345)
    {std::wregex(L"^使[0-9]{1}[A-HJ-NP-Z]{1}[0-9]{5}$")},

    // Hong Kong / Macau plate (e.g. 粤Z1234港)
    {std::wregex(L"^粤[Z]{1}[A-HJ-NP-Z0-9]{4}[港澳]{1}$")}};

const std::wstring kValidChars =
    L"京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼使领"  // Province abbreviations
    L"ABCDEFGHJKLMNPQRSTUVWXYZ"                                            // Plate letters (excluding I/O)
    L"0123456789"                                                          // Digits
    L"挂学警港澳";                                                         // Special identifiers

std::wstring FilterPlateChars(const std::wstring& input) {
    std::wstring result;

    for (wchar_t c : input) {
        // Convert to uppercase uniformly (does not affect Chinese chars)
        wchar_t upper_c = towupper(c);

        // Check if character is valid
        if (kValidChars.find(upper_c) != std::wstring::npos) {
            result.push_back(upper_c);
        }
    }

    return result;
}

#ifdef NVIDIA_SWITCH_ENABLE
std::wstring StringToWstring(const std::string& str_input) {
    if (str_input.empty()) {
        return L"";
    }
    std::string str_locale = setlocale(LC_ALL, "");
    const char* p_src      = str_input.c_str();
    size_t dest_size       = mbstowcs(nullptr, p_src, 0) + 1;
    auto sz_dest           = std::make_unique<wchar_t[]>(dest_size);
    wmemset(sz_dest.get(), 0, dest_size);
    mbstowcs(sz_dest.get(), p_src, dest_size);
    std::wstring wstr_result = sz_dest.get();
    setlocale(LC_ALL, str_locale.c_str());
    return FilterPlateChars(wstr_result);
}
#elif ASCEND_SWITCH_ENABLE
std::wstring StringToWstring(const std::string& str_input) {
    if (str_input.empty()) {
        return L"";
    }

    // Use std::wstring_convert and std::codecvt_utf8 for conversion
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

    try {
        // Convert std::string to std::wstring
        std::wstring wstr_result = converter.from_bytes(str_input);
        return FilterPlateChars(wstr_result);
    } catch (const std::range_error&) {
        // Conversion failed, return empty wide string
        return L"";
    }
}
#else
std::wstring StringToWstring(const std::string& /*str_input*/) {
    LOG_WARN("{}", "NOT SUPPORT");
    return L"";
}
#endif

std::string PlatePreProcess(std::string input) {
    std::string output;
    for (char& c : input) {
        auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (kCharReplacement.count(upper)) {
            output += kCharReplacement.at(upper);
        } else {
            output += upper;
        }
    }
    return output;
}

bool ValidatePlate(const std::string& input, std::string& output, size_t& wordSize) {
    std::string processed = PlatePreProcess(input);
    auto lProcessed       = StringToWstring(processed);
    wordSize              = lProcessed.size();
    // Try to match against all plate rules
    for (const auto& pattern : kPlateRules) {
        if (std::regex_match(lProcessed, pattern)) {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            output = converter.to_bytes(lProcessed);
            return true;
        }
    }
    return false;
}

// Vehicle type code mapping (GB standard)
/*
K33   	Car  	Sedan
K32		SUV		SUV
K20		Bus		Bus
K40		Van		Van
H		Truck	Truck
Z		Working	Special-purpose vehicle
Z501			Coal truck type 1 (coal truck analysis, V1.4.8.4)
Z502			Coal truck type 2 (coal truck analysis, V1.4.8.4)
Z503			Coal truck type 3 (coal truck analysis, V1.4.8.4)
N				Electric tricycle
M				Motorcycle
X				Other
*/
std::string GBVehicleType(const std::vector<AiAttribute>& attrRst) {
    for (auto& attr : attrRst) {
        if ("car type" == attr.category) {
            if ("car" == attr.label) {
                return "K33";
            } else if ("suv" == attr.label) {
                return "K32";
            } else if ("truck" == attr.label) {
                return "H";
            } else if ("tricycle" == attr.label) {
                return "N";
            } else if ("van" == attr.label) {
                return "K40";
            } else if ("bus" == attr.label) {
                return "K20";
            } else if ("working" == attr.label) {
                return "Z";
            }
        }
    }

    return "X";
}

GBVehicleColorType GBVehicleColor(const std::vector<AiAttribute>& attrRst) {
    for (auto& attr : attrRst) {
        if ("car color" == attr.category) {
            if ("black" == attr.label) {
                return GBVehicleColorType::kBlack;
            } else if ("white" == attr.label) {
                return GBVehicleColorType::kWhite;
            } else if ("silver" == attr.label) {
                return GBVehicleColorType::kGray;
            } else if ("red" == attr.label) {
                return GBVehicleColorType::kRed;
            } else if ("blue" == attr.label) {
                return GBVehicleColorType::kBlack;  // Assuming intentional mapping or copy error in original
                                                    // code
            } else if ("yellow" == attr.label) {
                return GBVehicleColorType::kYellow;
            } else if ("orange" == attr.label) {
                return GBVehicleColorType::kOrange;
            } else if ("brown" == attr.label) {
                return GBVehicleColorType::kBrown;
            } else if ("green" == attr.label) {
                return GBVehicleColorType::kGreen;
            } else if ("purple" == attr.label) {
                return GBVehicleColorType::kPurple;
            } else if ("mixcolor" == attr.label) {
                return GBVehicleColorType::kMixColor;
            } else if ("multicar" == attr.label) {
                return GBVehicleColorType::kMultiCar;
            } else if ("color_poorquality" == attr.label) {
                return GBVehicleColorType::kColorPoorQuality;
            }
        }
    }

    return GBVehicleColorType::kOther;
}

GBVehicleDirectionType GBVehicleDirection(const std::vector<AiAttribute>& attrRst) {
    for (auto& attr : attrRst) {
        if ("car direction" == attr.category) {
            if ("front" == attr.label) {
                return GBVehicleDirectionType::kFront;
            } else if ("back" == attr.label) {
                return GBVehicleDirectionType::kBack;
            } else if ("side" == attr.label) {
                return GBVehicleDirectionType::kSide;
            } else if ("direction_poorquality" == attr.label) {
                return GBVehicleDirectionType::kPoorQuality;
            }
        }
    }

    return GBVehicleDirectionType::kPoorQuality;
}

}  // namespace cosmo
