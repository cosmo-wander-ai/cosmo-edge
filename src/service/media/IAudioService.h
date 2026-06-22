/// @file IAudioService.h
/// @brief Audio service interface — manages audio files and audio output
///        devices for alarm sound playback.
#pragma once

#include <string>
#include <system_error>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/media/dto/AudioDeviceDto.h"

namespace cosmo::service {

/// CRUD and playback interface for audio alarm files and network audio
/// output devices (IP speakers).
///
/// Audio files are stored locally and served via the web server for
/// browser preview.  Audio devices are IP-addressable network speakers
/// used for real-time alarm playback.
class IAudioService {
public:
    virtual ~IAudioService() = default;

    // ── Audio File Management ──

    /// Query audio files with pagination and optional filters.
    /// @param totalSize    [out] Total matching file count.
    /// @param fileName     File name filter (empty for all).
    /// @param keepFileTypes File type filters (e.g. {"mp3", "wav"}).
    /// @param pageNum      Page number (1-based).
    /// @param pageSize     Page size.
    /// @return Vector of matching audio file info objects.
    virtual std::vector<cosmo::AlarmAudioInfo> QueryAudioFiles(int& totalSize, const std::string& fileName,
                                                               const std::vector<std::string>& keepFileTypes,
                                                               int pageNum, int pageSize) = 0;

    /// Get the web-accessible URL path for an audio file.
    /// @param info Audio file info object.
    /// @return Web URL path.
    virtual std::string GetAudioFileWebPath(cosmo::AlarmAudioInfo& info) = 0;

    /// Get the web-accessible URL path for an audio file by ID.
    /// @param id Audio file identifier.
    /// @return Web URL path.
    virtual std::string GetAudioFileWebPath(const std::string& id) = 0;

    /// Remove an audio file by ID.
    /// @param id  Audio file identifier.
    /// @param msg [out] Error message if removal fails (e.g. file in use).
    /// @return true on success.
    virtual bool RemoveAudioFile(const std::string& id, std::string& msg) = 0;

    /// Add a new audio file from an uploaded file.
    /// @param fileName Uploaded file name (resolved to the upload directory).
    /// @return true on success.
    virtual bool AddAudioFile(const std::string& fileName) = 0;

    /// Get the current number of stored audio files.
    virtual size_t AudioFileCount() const = 0;

    /// Get the maximum allowed number of audio files.
    virtual size_t AudioFileMaxCount() const = 0;

    // ── Audio Device Management ──

    /// Add a new network audio device (IP speaker).
    /// @param devName  Device display name.
    /// @param ip       Device IP address.
    /// @param ethName  Network interface name for communication.
    /// @param id       [out] Generated device ID.
    /// @return true on success.
    virtual bool AddAudioDevice(const std::string& devName, const std::string& ip, const std::string& ethName,
                                std::string& id) = 0;

    /// Modify an existing audio device configuration.
    /// @param id      Device identifier.
    /// @param ip      Updated IP address.
    /// @param name    Updated display name.
    /// @param ethName Updated network interface name.
    /// @return true on success.
    virtual bool ModifyAudioDevice(const std::string& id, const std::string& ip, const std::string& name,
                                   const std::string& ethName) = 0;

    /// Remove an audio device by ID.
    /// @param id  Device identifier.
    /// @param msg [out] Error message if removal fails (e.g. device in use).
    /// @return true on success.
    virtual bool RemoveAudioDevice(const std::string& id, std::string& msg) = 0;

    /// Query audio devices with pagination and optional name filter.
    /// @param totalSize [out] Total matching device count.
    /// @param name      Name filter (empty for all).
    /// @param pageNum   Page number (1-based).
    /// @param pageSize  Page size.
    /// @return Vector of matching audio device info objects.
    virtual std::vector<cosmo::AudioDeviceInfo> QueryAudioDevices(int& totalSize, const std::string& name,
                                                                  int pageNum, int pageSize) = 0;

    /// Check whether a network audio device is reachable.
    /// @param ip Device IP address.
    /// @return true if the device responds.
    virtual bool CheckAudioDeviceAlive(const std::string& ip) = 0;

    /// Play an audio file on a network audio device.
    /// @param info Playback request containing device ID and audio file ID.
    /// @return true on success.
    virtual bool PlayAudioDevice(cosmo::AudioDevicePlay& info) = 0;
};

// Dependency injection methods

}  // namespace cosmo::service
