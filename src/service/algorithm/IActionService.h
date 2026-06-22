/// @file IActionService.h
/// @brief Action service interface — decouples MessageHandler from
///        ActionMng/PActionMng singletons for algorithm action orchestration.
#pragma once

#include <string>

#include "service/detail/ServiceRegistry.h"
#include "util/dto/CosmoFwd.h"

namespace cosmo::service {

/// Provides CRUD access to algorithm action configurations.
///
/// Action algorithms define the processing pipeline for a given detection
/// scenario (e.g. person detection + tracking + classification).  This service
/// abstracts the underlying ActionMng (video) and PActionMng (image) managers
/// so that API handlers and task orchestrators can retrieve and update action
/// configurations without coupling to the concrete manager singletons.
class IActionService {
public:
    virtual ~IActionService() = default;

    // ── Video Algorithm Actions ──

    /// Retrieve a video action algorithm by code and version.
    /// @param code    Algorithm code identifier.
    /// @param version Algorithm version string.
    /// @return Shared pointer to the action algorithm, or nullptr if not found.
    virtual cosmo::ActionAlgPtr GetActionAlg(const std::string& code, const std::string& version) = 0;

    /// Retrieve a video action algorithm by code only (latest version).
    /// @param code Algorithm code identifier.
    /// @return Shared pointer to the action algorithm, or nullptr if not found.
    virtual cosmo::ActionAlgPtr GetActionAlgByCode(const std::string& code) = 0;

    /// Update a video action algorithm from a JSON string.
    /// @param actionAlgJson JSON representation of the action algorithm.
    /// @return true on success.
    virtual bool UpdateActionAlg(std::string& actionAlgJson) = 0;

    /// Update a video action algorithm from a structured object.
    /// @param actionAlg Action algorithm object to persist.
    /// @return true on success.
    virtual bool UpdateActionAlg(cosmo::ActionAlg& actionAlg) = 0;

    // ── Image Algorithm Actions ──

    /// Retrieve an image action algorithm by code and version.
    /// @param code    Algorithm code identifier.
    /// @param version Algorithm version string.
    /// @return Shared pointer to the action algorithm, or nullptr if not found.
    virtual cosmo::ActionAlgPtr GetPicActionAlg(const std::string& code, const std::string& version) = 0;

    /// Retrieve an image action algorithm by code only (latest version).
    /// @param code Algorithm code identifier.
    /// @return Shared pointer to the action algorithm, or nullptr if not found.
    virtual cosmo::ActionAlgPtr GetPicActionAlgByCode(const std::string& code) = 0;

    /// Update an image action algorithm from a JSON string.
    /// @param actionAlgJson JSON representation of the action algorithm.
    /// @return true on success.
    virtual bool UpdatePicActionAlg(std::string& actionAlgJson) = 0;

    /// Update an image action algorithm from a structured object.
    /// @param actionAlg Action algorithm object to persist.
    /// @return true on success.
    virtual bool UpdatePicActionAlg(cosmo::ActionAlg& actionAlg) = 0;
};

}  // namespace cosmo::service
