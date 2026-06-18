// ActionService implementation — manages video & picture algorithm orchestrations

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>

#include "service/algorithm/IActionService.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::service {

class ActionServiceImpl : public IActionService {
public:
    ActionServiceImpl();
    ~ActionServiceImpl();

    // ---- Video algorithm orchestration ----
    cosmo::ActionAlgPtr GetActionAlg(const std::string& code, const std::string& version) override;
    cosmo::ActionAlgPtr GetActionAlgByCode(const std::string& code) override;
    bool UpdateActionAlg(std::string& actionAlgJson) override;
    bool UpdateActionAlg(cosmo::ActionAlg& actionAlg) override;

    // ---- Picture algorithm orchestration ----
    cosmo::ActionAlgPtr GetPicActionAlg(const std::string& code, const std::string& version) override;
    cosmo::ActionAlgPtr GetPicActionAlgByCode(const std::string& code) override;
    bool UpdatePicActionAlg(std::string& actionAlgJson) override;
    bool UpdatePicActionAlg(cosmo::ActionAlg& actionAlg) override;

private:
    // ---- Unified helpers (shared by video and picture paths) ----

    /// Recursively expand LogicCalc key strings into keyLElements/keyRElements vectors.
    static void ExpandLogicValues(cosmo::LogicCalc& logic);

    /// Extract FPS and atomic algorithm code from configObject.params.
    /// The full-compat version handles multiple key name aliases for atomicCode.
    static void ExtractAtomAlgParams(cosmo::ActionNode& action, bool full_compat);

    /// Predicate type: returns true if the given action node needs atomic param extraction.
    using ActionPredicate = std::function<bool(const std::string& action_id)>;

    /// Predicate type: returns true if the given action node needs logic expansion.
    using LogicPredicate = std::function<bool(const std::string& action_id)>;

    /// Core workflow update logic shared by video and picture paths.
    bool DoUpdateActionAlg(cosmo::ActionAlg& action_alg_in, std::shared_mutex& mtx,
                           std::map<std::string, cosmo::ActionAlgPtr>& algs,
                           const ActionPredicate& needs_atom_params, const LogicPredicate& needs_logic_expand,
                           bool full_compat_atom_keys, const char* log_tag);

    /// Core JSON-to-struct update shared by video and picture paths.
    bool DoUpdateActionAlgFromJson(std::string& action_alg_json,
                                   std::function<bool(cosmo::ActionAlg&)> updater);

    /// Lookup by code+version in a guarded map.
    static cosmo::ActionAlgPtr FindByCodeVersion(std::shared_mutex& mtx,
                                                 const std::map<std::string, cosmo::ActionAlgPtr>& algs,
                                                 const std::string& code, const std::string& version);

    /// Lookup first match by algorithmCode in a guarded map.
    static cosmo::ActionAlgPtr FindByCode(std::shared_mutex& mtx,
                                          const std::map<std::string, cosmo::ActionAlgPtr>& algs,
                                          const std::string& code);

    // ---- Video orchestration storage ----
    std::shared_mutex video_mtx_;
    std::map<std::string, cosmo::ActionAlgPtr> video_algs_;

    // ---- Picture orchestration storage ----
    std::shared_mutex pic_mtx_;
    std::map<std::string, cosmo::ActionAlgPtr> pic_algs_;
};

}  // namespace cosmo::service
