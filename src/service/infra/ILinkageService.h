/// @file ILinkageService.h
/// @brief Linkage service interface — manages alarm output
///        devices (speakers, lights) and their linkage strategies.
#pragma once

#include <string>
#include <system_error>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/infra/dto/InfraMsgTypes.h"
#include "service/infra/dto/LinkageDto.h"

namespace cosmo::service {

/// CRUD and control interface for alarm output devices and their
/// linkage strategies (e.g. "on person detection → play audio on speaker X").
///
/// Consumed by the API layer for configuration management and by the flow
/// layer's alarm pipeline for runtime alarm triggering.
class ILinkageService {
public:
    virtual ~ILinkageService() = default;

    /// Add a new linkage strategy.
    /// @param name     Strategy display name.
    /// @param workFlow Workflow definition JSON.
    /// @param id       [out] Generated strategy ID.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Add(const std::string& name, const std::string& work_flow,
                                       std::string& id) = 0;

    /// Delete a linkage strategy.
    /// @param id Strategy identifier.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Delete(std::string& id) = 0;

    /// Update an existing linkage strategy.
    /// @param name     Updated display name.
    /// @param id       Strategy identifier.
    /// @param workFlow Updated workflow definition JSON.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Update(const std::string& name, const std::string& id,
                                          const std::string& work_flow) = 0;

    /// Query linkage strategies with pagination and optional name filter.
    /// @param pageNum  Page number (1-based).
    /// @param pageSize Page size.
    /// @param name     Name filter (empty for all).
    /// @param total    [out] Total matching record count.
    /// @return Vector of linkage strategy output units.
    virtual std::vector<cosmo::LinkageStrategyOutputUnit> Query(int page_num, int page_size,
                                                                const std::string& name, size_t& total) = 0;

    /// Enable or disable a linkage strategy.
    /// @param id     Strategy identifier.
    /// @param enable true to enable, false to disable.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Switch(std::string& id, bool enable) = 0;

    /// Read supported external storage device list.
    /// @param totalSize [out] Total number of storage devices.
    /// @param vecData   [out] Storage device descriptors.
    /// @return true on success.
    virtual bool ReadSupportedStorage(int& total_size, std::vector<cosmo::StorageList>& vec_data) = 0;

    /// Check whether an audio device is currently in use by any strategy.
    /// @param audioDeviceId Audio device identifier.
    /// @return true if in use.
    virtual bool IsAudioDeviceInUse(const std::string& audio_device_id) = 0;

    /// Check whether an audio file is currently in use by any strategy.
    /// @param audioFileId Audio file identifier.
    /// @return true if in use.
    virtual bool IsAudioFileInUse(const std::string& audio_file_id) = 0;

    /// Trigger an alarm action for a specific channel and algorithm.
    /// @param channelId Camera channel identifier.
    /// @param algId     Algorithm identifier that triggered the alarm.
    /// @return true if alarm was triggered successfully.
    virtual bool Alarm(const std::string& channelId, const std::string& algId) = 0;
};

// Dependency injection methods

}  // namespace cosmo::service
