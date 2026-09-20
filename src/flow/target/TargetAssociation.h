#pragma once

#include "infer/AiCommon.h"
#include "util/MsgBaseTypes.h"

namespace cosmo {

enum class AssociationRegion { kWhole, kUpperHalf, kLowerHalf };

// Defaults preserve AA_00006 configurations created before generic association.
struct TargetAssociationConfig {
    std::vector<std::string> labels{"face"};
    AssociationRegion region{AssociationRegion::kUpperHalf};
    double min_containment{0.9};
    int min_size{60};
    float confidence{0.66f};

    bool IsFaceAssociation() const {
        return labels.size() == 1 && labels.front() == "face";
    }
};

// Invalid updates leave the previous configuration intact. Legacy face parameter
// names are aliases; explicit generic names take precedence within one update.
bool UpdateTargetAssociationConfig(TargetAssociationConfig& config,
                                   const std::vector<MsgDynamicKeyValue>& params);

// One frame, one-to-one spatial association. All parent tracks survive, including
// missing, ambiguous and failed observations; only usable matches reach inference.
void AssociateTargets(std::vector<AiDetectRstEl>& parents, const std::vector<AiDetectRstEl>& children,
                      const TargetAssociationConfig& config, bool observation_complete = true);

}  // namespace cosmo
