// AudioServiceImpl — Audio Service Impl implementation.

#include "service/media/impl/AudioServiceImpl.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/network/IHttpClient.h"
#include "service/network/INetworkService.h"
#include "util/JsonFileUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {
namespace fs = std::filesystem;

static constexpr const char* kTag = "AudioServiceImpl";

AudioServiceImpl::AudioServiceImpl()
    : audio_db_file_(cosmo::path::GetCfgPath() + "/audioMng.json"),
      device_db_file_(cosmo::path::GetCfgPath() + "/AudioDeviceMng.json") {
    audio_file_path_ = cosmo::path::GetBaseDir() + audio_relative_path_;
    LOG_INFO("Init Cfg:{} FilePath:{}", audio_db_file_, audio_file_path_);
    {
        std::error_code err;
        if (!fs::exists(audio_file_path_, err)) {
            fs::create_directory(audio_file_path_, err);
        }
    }

    LoadAudio();
    LoadAudioDevice();
    bool find_default = std::any_of(audio_info_list_.begin(), audio_info_list_.end(),
                                    [&](const auto& audio) { return audio.name == default_audio_name_; });

    // Default audio
    if (!find_default) {
        AddDefaultAudio();
    }
    LOG_INFO("{}", "AudioServiceImpl Init Ok");
}

AudioServiceImpl::~AudioServiceImpl() {
    LOG_INFO("{}", "AudioServiceImpl Quit");
}

bool AudioServiceImpl::LoadAudio() {
    fs::path filePath(audio_db_file_);
    std::error_code err;
    if (fs::exists(filePath, err)) {
        try {
            nlohmann::json doc;
            auto ret = util::JsonFileUtil::ReadJsonArray(audio_db_file_, doc);
            if (ret != util::ErrorEnum::Success) {
                LOG_WARN("Failed to read audio db file: {}", audio_db_file_);
                return false;
            }
            audio_info_list_ = doc.get<std::vector<cosmo::AlarmAudioInfo>>();
            return true;
        } catch (const std::exception& e) {
            LOG_ERRO("{}", e.what());
        }
    }
    return false;
}

void AudioServiceImpl::SaveJsonToFile(const std::string& path, const std::string& json_str) {
    if (json_str.empty()) {
        return;
    }
    std::ofstream ofile(path);
    if (ofile.is_open()) {
        ofile << json_str;
    } else {
        LOG_WARN("Failed to open file for writing: {}", path);
    }
}

// ── Audio file management ──

std::vector<cosmo::AlarmAudioInfo> AudioServiceImpl::QueryAudioFiles(
    int& total_size, const std::string& file_name, const std::vector<std::string>& keep_file_types,
    int page_num, int page_size) {
    if ((page_num <= 0) || (page_size <= 0)) {
        return {};
    }

    size_t index = (page_num - 1) * page_size;
    std::shared_lock<std::shared_mutex> lock(audio_mtx_);
    total_size = static_cast<int>(audio_info_list_.size());
    if (index >= audio_info_list_.size()) {
        LOG_WARN("page_num:{}, page_size:{} Get index:{} >= audioSize:{}", page_num, page_size, index,
                 audio_info_list_.size());
        return {};
    }

    size_t index_max = index + page_size;
    index_max        = index_max >= audio_info_list_.size() ? audio_info_list_.size() : index_max;

    if ((!file_name.empty()) || (!keep_file_types.empty())) {
        return FilterAudio(file_name, keep_file_types, index, index_max, total_size);
    }

    std::vector<cosmo::AlarmAudioInfo> audio_list;
    for (size_t i = index; i < index_max; i++) {
        audio_list.push_back(audio_info_list_[i]);
    }
    LOG_INFO("page_num:{}, page_size:{} AudioSize:{} index:{}, index_max:{} Get OutSize:{}", page_num,
             page_size, audio_info_list_.size(), index, index_max, audio_list.size());
    return audio_list;
}

std::vector<cosmo::AlarmAudioInfo> AudioServiceImpl::FilterAudio(
    const std::string& file_name, const std::vector<std::string>& keep_file_types, size_t index,
    size_t index_max, int& total_size) {
    std::vector<cosmo::AlarmAudioInfo> audio_list;
    size_t find_count = 0;
    bool b_file_find  = !file_name.empty();
    bool bkeep_files  = !keep_file_types.empty();
    for (auto& info : audio_info_list_) {
        if ((b_file_find && (info.name.find(file_name) != std::string::npos)) ||
            (bkeep_files && AudioFileInList(info.fileName, keep_file_types))) {
            find_count++;
            if (find_count > index_max) {
                continue;
            }
            if (find_count > index) {
                audio_list.push_back(info);
            }
        }
    }
    total_size = static_cast<int>(find_count);
    return audio_list;
}

bool AudioServiceImpl::AudioFileInList(const std::string& file_name,
                                       const std::vector<std::string>& keep_file_types) {
    auto file_type = GetAudioFileType(file_name);
    return std::any_of(keep_file_types.begin(), keep_file_types.end(),
                       [&file_type](const auto& keepFile) { return file_type == keepFile; });
}

std::string AudioServiceImpl::GetAudioFileWebPath(cosmo::AlarmAudioInfo& info) {
    return audio_relative_path_ + info.fileName;
}

std::string AudioServiceImpl::GetAudioFileWebPath(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(audio_mtx_);
    auto it = std::find_if(audio_info_list_.begin(), audio_info_list_.end(),
                           [&id](const auto& audio) { return audio.id == id; });
    if (it != audio_info_list_.end()) {
        return audio_relative_path_ + it->fileName;
    }
    LOG_WARN("Cant Find Audio id:{} Use Default Audio", id);
    return "";
}

bool AudioServiceImpl::RemoveAudioFile(const std::string& id, std::string& msg) {
    LOG_INFO("Remove Id:{}", id);
    std::string json_to_save;
    {
        std::lock_guard<std::shared_mutex> lock(audio_mtx_);
        auto iter_find = std::find_if(audio_info_list_.begin(), audio_info_list_.end(),
                                      [&](const cosmo::AlarmAudioInfo& info) { return info.id == id; });
        if (iter_find != audio_info_list_.end()) {
            if (iter_find->name == default_audio_name_) {
                msg = "Default audio cannot be deleted";
                return false;
            }
            std::error_code err;
            fs::remove(GetAudioPath(*iter_find), err);
            LOG_INFO("Remove {} Ok Id:{}", iter_find->name, iter_find->id);
            audio_info_list_.erase(iter_find);
            json_to_save = nlohmann::json(audio_info_list_).dump(2);
        } else {
            LOG_WARN("Remove Id:{} But It Not Exist", id);
            msg = "File Not Exist";
            return false;
        }
    }
    SaveJsonToFile(audio_db_file_, json_to_save);
    return true;
}

bool AudioServiceImpl::AddAudioFile(const std::string& file_name) {
    fs::path filePath{file_name};
    LOG_INFO("Add {} ", file_name);
    cosmo::AlarmAudioInfo info;
    std::error_code err;
    if (fs::exists(filePath, err)) {
        info.id        = cosmo::util::GenerateUUID();
        info.name      = filePath.filename();
        info.timestamp = cosmo::util::GetMilliseconds();
        GetAudioFileName(filePath.filename(), info);

        std::string audio_file_path = GetAudioPath(info);
        std::string json_to_save;

        {
            std::lock_guard<std::shared_mutex> lock(audio_mtx_);
            // Resolve the legacy TOCTOU race by checking for name collision inside the lock scope
            auto it = std::find_if(audio_info_list_.begin(), audio_info_list_.end(),
                                   [&](const auto& a) { return a.name == filePath.filename(); });
            if (it != audio_info_list_.end()) {
                LOG_WARN("{} In Audio List, Cant Add It.", file_name);
                return false;
            }

            fs::rename(filePath, audio_file_path, err);
            if (err) {
                LOG_ERRO("Rename {} to {} failed: {}", filePath.string(), audio_file_path, err.message());
                return false;
            }

            audio_info_list_.push_back(info);
            json_to_save = nlohmann::json(audio_info_list_).dump(2);
        }
        SaveJsonToFile(audio_db_file_, json_to_save);
        LOG_INFO("Add {} Ok Id:{}", file_name, info.id);
        return true;
    }
    LOG_WARN("Add {} But File Not Exist", file_name);
    return false;
}

size_t AudioServiceImpl::AudioFileCount() const {
    std::shared_lock<std::shared_mutex> lock(audio_mtx_);
    return audio_info_list_.size();
}

size_t AudioServiceImpl::AudioFileMaxCount() const {
    return max_audio_count_;
}

bool AudioServiceImpl::AudioNameExist(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(audio_mtx_);
    return std::any_of(audio_info_list_.begin(), audio_info_list_.end(),
                       [&name](const auto& audio) { return audio.name == name; });
}

bool AudioServiceImpl::AudioIdExist(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(audio_mtx_);
    return std::any_of(audio_info_list_.begin(), audio_info_list_.end(),
                       [&id](const auto& audio) { return audio.id == id; });
}

cosmo::AlarmAudioInfo AudioServiceImpl::AddDefaultAudio() {
    cosmo::AlarmAudioInfo info;
    LOG_INFO("{}", "Add Default Audio");
    fs::path filePath{default_audio_file_name_};

    info.name      = default_audio_name_;
    info.id        = default_audio_id_;
    info.fileName  = filePath.filename();
    info.timestamp = cosmo::util::GetMilliseconds();

    std::string audio_file_path = GetAudioPath(info);
    std::error_code err;
    fs::copy_file(default_audio_file_name_, audio_file_path, err);
    if (err) {
        LOG_ERRO("Copy default audio failed: {}", err.message());
    }

    std::string json_to_save;
    {
        std::lock_guard<std::shared_mutex> lock(audio_mtx_);
        audio_info_list_.push_back(info);
        json_to_save = nlohmann::json(audio_info_list_).dump(2);
    }
    SaveJsonToFile(audio_db_file_, json_to_save);

    return info;
}

void AudioServiceImpl::GetAudioFileName(const std::string& file_name, cosmo::AlarmAudioInfo& info) {
    auto file_type = GetAudioFileType(file_name);
    if (file_type.empty()) {
        info.fileName = info.id;
    } else {
        info.fileName = info.id + "." + file_type;
    }
}

std::string AudioServiceImpl::GetAudioPath(const cosmo::AlarmAudioInfo& info) {
    return audio_file_path_ + info.fileName;
}

std::string AudioServiceImpl::GetAudioFileType(const std::string& file_name) {
    size_t dot_pos = file_name.rfind('.');
    if (dot_pos != std::string::npos) {
        return file_name.substr(dot_pos + 1);
    }
    return "";
}

// ── Audio column device management ──

// Audio playback — moved to AudioServicePlayback.cc

}  // namespace cosmo::service
