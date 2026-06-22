#pragma once

#include <shared_mutex>
#include <string>
#include <vector>

#include "service/media/IAudioService.h"
#include "service/media/dto/AudioDeviceDto.h"

namespace cosmo::service {

class AudioServiceImpl : public IAudioService {
public:
    AudioServiceImpl();
    ~AudioServiceImpl() override;

    // ── Audio file management ──
    std::vector<cosmo::AlarmAudioInfo> QueryAudioFiles(int& totalSize, const std::string& fileName,
                                                       const std::vector<std::string>& keepFileTypes,
                                                       int pageNum, int pageSize) override;
    std::string GetAudioFileWebPath(cosmo::AlarmAudioInfo& info) override;
    std::string GetAudioFileWebPath(const std::string& id) override;
    bool RemoveAudioFile(const std::string& id, std::string& msg) override;
    bool AddAudioFile(const std::string& fileName) override;
    size_t AudioFileCount() const override;
    size_t AudioFileMaxCount() const override;

    // ── Audio column device management ──
    bool AddAudioDevice(const std::string& devName, const std::string& ip, const std::string& ethName,
                        std::string& id) override;
    bool ModifyAudioDevice(const std::string& id, const std::string& ip, const std::string& name,
                           const std::string& ethName) override;
    bool RemoveAudioDevice(const std::string& id, std::string& msg) override;
    std::vector<cosmo::AudioDeviceInfo> QueryAudioDevices(int& totalSize, const std::string& name,
                                                          int pageNum, int pageSize) override;
    bool CheckAudioDeviceAlive(const std::string& ip) override;
    bool PlayAudioDevice(cosmo::AudioDevicePlay& info) override;

private:
    bool LoadAudio();
    bool AudioNameExist(const std::string& name);
    bool AudioIdExist(const std::string& id);

    cosmo::AlarmAudioInfo AddDefaultAudio();

    std::string GetAudioPath(const cosmo::AlarmAudioInfo& info);
    std::string GetAudioFileType(const std::string& fileName);
    void GetAudioFileName(const std::string& fileName, cosmo::AlarmAudioInfo& info);

    std::vector<cosmo::AlarmAudioInfo> FilterAudio(const std::string& fileName,
                                                   const std::vector<std::string>& keepFileTypes,
                                                   size_t index, size_t indexMax, int& totalSize);
    bool AudioFileInList(const std::string& fileName, const std::vector<std::string>& keepFileTypes);

    /// Write pre-serialized JSON to a file (called outside lock scope).
    static void SaveJsonToFile(const std::string& path, const std::string& json_str);

    bool LoadAudioDevice();
    std::vector<cosmo::AudioDeviceInfo> FilterAudioDevice(const std::string& devName, size_t index,
                                                          size_t indexMax, int& totalSize);

    bool DeviceIpExist(const std::string& ip);
    bool DeviceNameExist(const std::string& name);
    bool DeviceIdExist(const std::string& id);
    bool GetAudioDevice(const std::string& id, cosmo::AudioDeviceInfo& info);

    bool HttpSubmit(const std::string& url, const std::string& data);

    // :: Audio state
    mutable std::shared_mutex audio_mtx_;
    std::string audio_db_file_;
    std::string audio_file_path_;
    size_t max_audio_count_{1000};
    std::vector<cosmo::AlarmAudioInfo> audio_info_list_;
    std::string audio_relative_path_     = "/audioMng/";
    std::string default_audio_name_      = "默认";                    // Default audio (business label)
    std::string default_audio_id_        = "1234567890";              // Default audio ID
    std::string default_audio_file_name_ = "./media/audio/beep.ogg";  // Default audio file
    std::string default_audio_web_file_  = "/audioMng/beep.ogg";

    // :: AudioDevice state
    mutable std::shared_mutex device_mtx_;
    std::string device_db_file_;
    size_t max_dev_count_{1000};
    std::vector<cosmo::AudioDeviceInfo> device_info_list_;
};

}  // namespace cosmo::service
