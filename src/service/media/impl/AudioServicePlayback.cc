// AudioServicePlayback.cc — Audio playback for AudioServiceImpl.
// Split from AudioServiceImpl.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/media/impl/AudioServiceImpl.h"
#include "service/network/IHttpClient.h"
#include "service/network/INetworkService.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

namespace fs = std::filesystem;

static constexpr const char* kTag = "AudioServiceImpl";
constexpr int kMinIpLength        = 7;  // Minimum valid IP length (e.g. "1.1.1.1")

namespace {
    struct AudioDeviceMsgPlayLoop {
        int duration{600};  // Loop playback duration (seconds)
        int times{20};      // Loop playback repeat count
        int gap{2};         // Gap between loop iterations (seconds)
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AudioDeviceMsgPlayLoop, duration, times, gap)
    };

    struct AudioDeviceMsgPlayUrl {
        std::string url;
        bool sync{false};
        bool queue{false};
        int volume{50};
        bool prompt{false};
        AudioDeviceMsgPlayLoop loop;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AudioDeviceMsgPlayUrl, url, sync, queue, volume, prompt,
                                                    loop)
    };

    struct AudioDeviceMsgPlayText {
        std::string text;
        std::string vcn{"xiaofeng"};
        cosmo::AudioDeviceVoiceTone tone{cosmo::AudioDeviceVoiceTone::AudioDeviceVoiceToneMale};
        int speed{50};
        int volume{50};
        std::string rdn{"0"};
        std::string rcn{"0"};
        int reg{0};
        bool sync{false};
        bool queue{false};
        bool prompt{false};
        AudioDeviceMsgPlayLoop loop;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AudioDeviceMsgPlayText, text, vcn, speed, volume, rdn,
                                                    rcn, reg, sync, queue, prompt, loop)
    };
}  // namespace

bool AudioServiceImpl::LoadAudioDevice() {
    fs::path filePath(device_db_file_);
    std::error_code err;
    if (fs::exists(filePath, err)) {
        try {
            std::ifstream ifs(device_db_file_);
            if (ifs.is_open()) {
                auto j = nlohmann::json::parse(ifs);
                j.get_to(device_info_list_);
                return true;
            }
        } catch (const std::exception& e) {
            LOG_ERRO("{}", e.what());
        }
    }
    return false;
}

bool AudioServiceImpl::AddAudioDevice(const std::string& devName, const std::string& ip,
                                      const std::string& ethName, std::string& id) {
    LOG_INFO("Add {} ", devName);
    cosmo::AudioDeviceInfo info;
    info.id        = cosmo::util::GenerateUUID();
    info.name      = devName;
    info.ip        = ip;
    info.ethName   = ethName;
    info.timestamp = cosmo::util::GetMilliseconds();

    std::string json_to_save;
    {
        std::lock_guard<std::shared_mutex> lock(device_mtx_);
        // Fix TOCTOU: Check IP inside lock
        auto it = std::find_if(device_info_list_.begin(), device_info_list_.end(),
                               [&](const auto& dev) { return dev.ip == ip; });
        if (it != device_info_list_.end()) {
            LOG_WARN("Add {} But {} Is Exist.", devName, ip);
            return false;
        }

        device_info_list_.push_back(info);
        json_to_save = nlohmann::json(device_info_list_).dump(2);
    }
    SaveJsonToFile(device_db_file_, json_to_save);
    LOG_INFO("Add {} Ok Id:{}", devName, info.id);
    id = info.id;
    return true;
}

bool AudioServiceImpl::ModifyAudioDevice(const std::string& id, const std::string& ip,
                                         const std::string& name, const std::string& ethName) {
    std::string json_to_save;
    {
        std::lock_guard<std::shared_mutex> lock(device_mtx_);
        auto it = std::find_if(device_info_list_.begin(), device_info_list_.end(),
                               [&id](const auto& audio) { return audio.id == id; });
        if (it != device_info_list_.end()) {
            if (!name.empty()) {
                LOG_INFO("{} Modify Name From {} To {}", id, it->name, name);
                it->name = name;
            }
            if (!ethName.empty()) {
                LOG_INFO("{} Modify ethName From {} To {}", id, it->ethName, ethName);
                it->ethName = ethName;
            }
            if (!ip.empty()) {
                LOG_INFO("{} Modify ip From {} To {}", id, it->ip, ip);
                it->ip = ip;
            }
            json_to_save = nlohmann::json(device_info_list_).dump(2);
        } else {
            return false;
        }
    }
    SaveJsonToFile(device_db_file_, json_to_save);
    return true;
}

bool AudioServiceImpl::RemoveAudioDevice(const std::string& id, std::string& msg) {
    LOG_INFO("Remove Id:{}", id);
    std::string json_to_save;
    {
        std::lock_guard<std::shared_mutex> lock(device_mtx_);
        auto iter_find = std::find_if(device_info_list_.begin(), device_info_list_.end(),
                                      [&](const cosmo::AudioDeviceInfo& info) { return info.id == id; });
        if (iter_find != device_info_list_.end()) {
            LOG_INFO("Remove {} Ok Id:{}", iter_find->name, iter_find->id);
            device_info_list_.erase(iter_find);
            json_to_save = nlohmann::json(device_info_list_).dump(2);
        } else {
            LOG_WARN("Remove Id:{} But It Not Exist", id);
            msg = "Device Not Exist";
            return false;
        }
    }
    SaveJsonToFile(device_db_file_, json_to_save);
    return true;
}

std::vector<cosmo::AudioDeviceInfo> AudioServiceImpl::FilterAudioDevice(const std::string& name, size_t index,
                                                                        size_t index_max, int& total_size) {
    std::vector<cosmo::AudioDeviceInfo> audio_list;
    size_t find_count = 0;
    for (auto& info : device_info_list_) {
        if (info.name.find(name) != std::string::npos) {
            find_count++;
            if (find_count > index_max) {
                continue;
            }
            if ((find_count > index)) {
                audio_list.push_back(info);
            }
        }
    }
    total_size = static_cast<int>(find_count);
    return audio_list;
}

std::vector<cosmo::AudioDeviceInfo> AudioServiceImpl::QueryAudioDevices(int& total_size,
                                                                        const std::string& name, int page_num,
                                                                        int page_size) {
    LOG_INFO("page_num:{}, page_size:{}", page_num, page_size);
    if ((page_num <= 0) || (page_size <= 0)) {
        return {};
    }

    size_t index = (page_num - 1) * page_size;

    std::shared_lock<std::shared_mutex> lock(device_mtx_);
    total_size = static_cast<int>(device_info_list_.size());
    if (index >= device_info_list_.size()) {
        LOG_WARN("page_num:{}, page_size:{} Get index:{} >= audioSize:{}", page_num, page_size, index,
                 device_info_list_.size());
        return {};
    }

    size_t index_max = index + page_size;

    index_max = index_max >= device_info_list_.size() ? device_info_list_.size() : index_max;

    if (!name.empty()) {
        return FilterAudioDevice(name, index, index_max, total_size);
    }

    std::vector<cosmo::AudioDeviceInfo> audio_list;
    for (size_t i = index; i < index_max; i++) {
        auto info = device_info_list_[i];
        audio_list.push_back(info);
    }
    LOG_INFO("page_num:{}, page_size:{} AudioSize:{} index:{}, index_max:{} Get OutSize:{}", page_num,
             page_size, device_info_list_.size(), index, index_max, audio_list.size());
    return audio_list;
}

bool AudioServiceImpl::DeviceIpExist(const std::string& ip) {
    std::shared_lock<std::shared_mutex> lock(device_mtx_);
    return std::any_of(device_info_list_.begin(), device_info_list_.end(),
                       [&ip](const auto& audio) { return audio.ip == ip; });
}

bool AudioServiceImpl::DeviceNameExist(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(device_mtx_);
    return std::any_of(device_info_list_.begin(), device_info_list_.end(),
                       [&name](const auto& audio) { return audio.name == name; });
}

bool AudioServiceImpl::DeviceIdExist(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(device_mtx_);
    return std::any_of(device_info_list_.begin(), device_info_list_.end(),
                       [&id](const auto& audio) { return audio.id == id; });
}

bool AudioServiceImpl::GetAudioDevice(const std::string& id, cosmo::AudioDeviceInfo& info) {
    std::shared_lock<std::shared_mutex> lock(device_mtx_);
    auto it = std::find_if(device_info_list_.begin(), device_info_list_.end(),
                           [&id](const auto& audio) { return audio.id == id; });
    if (it != device_info_list_.end()) {
        info = *it;
        return true;
    }

    return false;
}

struct AudioDeviceRsp {
    int code{0};
    std::string message;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AudioDeviceRsp, code, message)
};

bool AudioServiceImpl::HttpSubmit(const std::string& url, const std::string& data) {
    auto response = ServiceRegistry::Instance().Get<cosmo::service::IHttpClient>().Post(url, data);
    if (response.statusCode != 200) {
        LOG_ERRO("Request {} , data:{} status: {}", url, data, response.statusCode);
        return false;
    }

    // Get result
    auto ret_json = response.body;
    // Print result
    LOG_INFO("From {} Submit {} Get : {}", url, data, ret_json);
    AudioDeviceRsp rsp;
    try {
        auto j = nlohmann::json::parse(ret_json);
        j.get_to(rsp);
    } catch (const std::exception& e) {
        LOG_ERRO("Parse:{} Get {} ", ret_json, e.what());
        return false;
    }

    if (200 != rsp.code) {
        return false;
    }

    return true;
}

bool AudioServiceImpl::CheckAudioDeviceAlive(const std::string& ip) {
    std::string url = "http://" + ip + "/v1/check_alive";
    std::string data;
    return HttpSubmit(url, data);
}

bool AudioServiceImpl::PlayAudioDevice(cosmo::AudioDevicePlay& info) {
    cosmo::AudioDeviceInfo devInfo;
    if (!GetAudioDevice(info.devId, devInfo)) {
        LOG_WARN("Cant Get DevId:{}", info.devId);
        return false;
    }
    bool b_main_card = false;
    if ((devInfo.ethName == cosmo::service::kMainCardName) ||
        (devInfo.ethName == cosmo::service::kMainEthName)) {
        b_main_card = true;
    }
    auto net_cards = service::ServiceRegistry::Instance().Get<service::INetworkService>().GetNetCards();
    std::string ip;
    auto it = std::find_if(net_cards.begin(), net_cards.end(),
                           [b_main_card](const auto& netCard) { return netCard.isMain == b_main_card; });
    if (it != net_cards.end()) {
        ip = it->ipAddr;
    }
    // 1.1.1.1  size = 7
    if (ip.size() < kMinIpLength) {
        LOG_WARN("Invalid ip:{}", ip);
        return false;
    }

    if (cosmo::AudioDevicePlayType::AudioDevicePlayTypeAudioPlay == info.playType) {
        auto audio_web_path = ServiceRegistry::Instance().Get<IAudioService>().GetAudioFileWebPath(info.data);
        if (audio_web_path.empty()) {
            LOG_WARN("Invlaid data:{}", info.data);
            return false;
        }
        AudioDeviceMsgPlayUrl PlayUrlData;
        PlayUrlData.url           = "http://" + ip + audio_web_path;
        PlayUrlData.volume        = info.volume;
        PlayUrlData.loop.duration = info.duration;
        PlayUrlData.loop.gap      = info.gap;
        PlayUrlData.loop.times    = info.times;
        std::string url           = "http://" + devInfo.ip + "/v1/speech";
        std::string data_str;
        try {
            data_str = nlohmann::json(PlayUrlData).dump();
        } catch (const std::exception& e) {
            LOG_ERRO("{}", e.what());
            return false;
        }
        return HttpSubmit(url, data_str);
    } else if (cosmo::AudioDevicePlayType::AudioDevicePlayTypeTextPlay == info.playType) {
        AudioDeviceMsgPlayText textData;
        textData.text          = info.data;
        textData.tone          = info.tone;
        textData.speed         = info.speed;
        textData.volume        = info.volume;
        textData.loop.duration = info.duration;
        textData.loop.gap      = info.gap;
        textData.loop.times    = info.times;
        std::string url        = "http://" + devInfo.ip + "/v1/speech";
        if (cosmo::AudioDeviceVoiceTone::AudioDeviceVoiceToneMale == textData.tone) {
            textData.vcn = "xiaofeng";
        } else {
            textData.vcn = "xiaoyan";
        }
        std::string data_str;
        try {
            data_str = nlohmann::json(textData).dump();
        } catch (const std::exception& e) {
            LOG_ERRO("{}", e.what());
            return false;
        }
        return HttpSubmit(url, data_str);
    } else {
        LOG_WARN("Invlaid playType:{}", info.playType);
        return false;
    }
}

}  // namespace cosmo::service
