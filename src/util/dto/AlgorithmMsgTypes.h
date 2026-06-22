// Algorithm types — ActionBase, ActionAlg, ActionAlgPtr.
// Modular DTO header.

#pragma once

#include "util/LogicCalc.h"
#include "util/MsgBaseTypes.h"

namespace cosmo {

struct ActionBase {
    std::string flowActionId{"-1"};     // Pipeline ID
    std::string preFlowActionId{"-1"};  // Parent pipeline ID
    float initFps{-1.0};  // Base FPS; parsed from derived class, extracted from configObject.params
    virtual ~ActionBase() = default;
};

void to_json(nlohmann::json& j, const ActionBase& b);
void from_json(const nlohmann::json& j, ActionBase& b);

using ActionBasePtr = std::shared_ptr<ActionBase>;

// -------------- AA: Atomic Action — atomic algorithm actions -------------------------
// Detection algorithm
struct MvActionConfigObject {
    std::vector<MsgDynamicKeyValue> params;
    LogicCalc condition;
    friend void to_json(nlohmann::json& j, const MvActionConfigObject& v);
    friend void from_json(const nlohmann::json& j, MvActionConfigObject& v);
};
using MvActionConfigObjectPtr = std::shared_ptr<MvActionConfigObject>;

// Different orchestrations may use CCA/CECA/CEA for different structures
struct ActionNode : public ActionBase {
    std::string actionId;
    std::string actionName;
    std::string atomAlgName;  // Atomic algorithm prefix (with path), extracted from configObject.params
    std::string atomicCode;   // Atomic algorithm code, extracted from configObject.params
    MvActionConfigObject configObject;
};

void to_json(nlohmann::json& j, const ActionNode& n);
void from_json(const nlohmann::json& j, ActionNode& n);

// Orchestrated algorithm
struct ActionAlg {
    std::string algorithmName;        // Business algorithm name
    std::string algorithmCode;        // Business algorithm code
    std::string algorithmUpdateTime;  // Business algorithm version
    std::string algorithmCheckSum;    // Model update checksum
    std::string category;
    float algorithmMinFps{-1.0};
    std::vector<ActionNode> workFlow;
};

void to_json(nlohmann::json& j, const ActionAlg& a);
void from_json(const nlohmann::json& j, ActionAlg& a);

using ActionAlgPtr = std::shared_ptr<ActionAlg>;

enum class MsgTaskActionType {
    MsgTaskActionTypeVideo = 0,
    MsgTaskActionTypePicture

    ,
    MsgTaskActionTypeMax  //
};
struct MsgLoadLocalAlgorithmActionRecv : public MsgRecvHead {
    MsgTaskActionType taskActionType{MsgTaskActionType::MsgTaskActionTypeVideo};
    std::string fileName;
    ActionAlg action;
};

void to_json(nlohmann::json& j, const MsgLoadLocalAlgorithmActionRecv& r);
void from_json(const nlohmann::json& j, MsgLoadLocalAlgorithmActionRecv& r);

struct MsgLoadLocalAlgorithmActionSend : public MsgSendHead {};

struct MsgAlgorithmMetaDataRegion {
    std::vector<MsgDynamicElement> heads;
    friend void to_json(nlohmann::json& j, const MsgAlgorithmMetaDataRegion& v);
    friend void from_json(const nlohmann::json& j, MsgAlgorithmMetaDataRegion& v);
};

struct MsgAlgorithmMetaData {
    std::vector<MsgDynamicElement> params;
    MsgAlgorithmMetaDataRegion region;
    MsgAlgorithmMetaDataRegion shieldedRegion;
    std::string regionType;
    bool scheduleSupport{true};
    bool defaultFullScreen{true};
    size_t maxAreaCount{4};
    friend void to_json(nlohmann::json& j, const MsgAlgorithmMetaData& v);
    friend void from_json(const nlohmann::json& j, MsgAlgorithmMetaData& v);
};

}  // namespace cosmo
