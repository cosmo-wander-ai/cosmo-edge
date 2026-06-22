// MessageAlgorithmHandler — Algorithm message handler.

#pragma once

#include <system_error>

#include "service/algorithm/dto/AlgorithmDto.h"

namespace cosmo::service {
class IAlgorithmQuery;
class IAlgorithmCrud;
class IAlgorithmLayout;
class ICameraTaskConfig;
class ITaskQuery;
}  // namespace cosmo::service

namespace cosmo {

// Route requests to appropriate handler methods based on message type
class MessageAlgorithmHandler {
public:
    MessageAlgorithmHandler(service::IAlgorithmQuery& algorithm_query,
                            service::IAlgorithmCrud& algorithm_crud,
                            service::IAlgorithmLayout& algorithm_layout,
                            service::ICameraTaskConfig& camera_task_config, service::ITaskQuery& task_query);
    ~MessageAlgorithmHandler() = default;

    // Query algorithm list
    [[nodiscard]] Algorithm::MsgPageSend Handle(Algorithm::MsgPageRecv&& data,
                                                std::error_condition& errc) const;

    // Upload algorithm package
    [[nodiscard]] Algorithm::MsgUploadSend Handle(Algorithm::MsgUploadRecv&& data,
                                                  std::error_condition& errc) const;

    // Edit algorithm
    [[nodiscard]] Algorithm::MsgUpdateSend Handle(Algorithm::MsgUpdateRecv&& data,
                                                  std::error_condition& errc) const;

    // Delete algorithm
    [[nodiscard]] Algorithm::MsgDeleteSend Handle(Algorithm::MsgDeleteRecv&& data,
                                                  std::error_condition& errc) const;

    // =========================================================================
    // AIBox platform specific APIs
    // =========================================================================

    // Add algorithm
    [[nodiscard]] Algorithm::MsgAddSend Handle(Algorithm::MsgAddRecv&& data,
                                               std::error_condition& errc) const;

    // Save orchestrated algorithm
    [[nodiscard]] Algorithm::MsgLayoutSaveSend Handle(Algorithm::MsgLayoutSaveRecv&& data,
                                                      std::error_condition& errc) const;

    // Query orchestrated algorithm details
    [[nodiscard]] Algorithm::MsgLayoutDetailSend Handle(Algorithm::MsgLayoutDetailRecv&& data,
                                                        std::error_condition& errc) const;

    // Query orchestrated algorithm list
    [[nodiscard]] Algorithm::MsgLayoutListSend Handle(Algorithm::MsgLayoutListRecv&& data,
                                                      std::error_condition& errc) const;

    // Export single orchestrated algorithm
    [[nodiscard]] Algorithm::MsgLayoutExportSingleSend Handle(Algorithm::MsgLayoutExportSingleRecv&& data,
                                                              std::error_condition& errc) const;

    // Batch export orchestrated algorithms
    [[nodiscard]] Algorithm::MsgLayoutExportAllSend Handle(Algorithm::MsgLayoutExportAllRecv&& data,
                                                           std::error_condition& errc) const;

    // Query orchestrated action list
    [[nodiscard]] Algorithm::MsgAtomicActionListSend Handle(Algorithm::MsgAtomicActionListRecv&& data,
                                                            std::error_condition& errc) const;

    // Query pass flow list
    [[nodiscard]] Algorithm::MsgPassFlowListSend Handle(Algorithm::MsgPassFlowListRecv&& data,
                                                        std::error_condition& errc) const;

private:
    service::IAlgorithmQuery& algorithm_query_;
    service::IAlgorithmCrud& algorithm_crud_;
    service::IAlgorithmLayout& algorithm_layout_;
    service::ICameraTaskConfig& camera_task_config_;
    service::ITaskQuery& task_query_;
};

}  // namespace cosmo
