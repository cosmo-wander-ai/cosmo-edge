// MessageAudioHandler — Message Audio Handler implementation.

#include "api/MessageAudioHandler.h"

#include <future>

#include "service/infra/ILinkageService.h"
#include "service/media/IAudioService.h"
#include "util/Log.h"

namespace cosmo {

static constexpr const char* kTag = "MessageAudioHandler";

MessageAudioHandler::MessageAudioHandler(service::IAudioService& audio_svc,
                                         service::ILinkageService& linkage_svc)
    : audio_svc_(audio_svc), linkage_svc_(linkage_svc) {}

// Audio file query
Audio::MsgQueryAudioFileSend MessageAudioHandler::Handle(Audio::MsgQueryAudioFileRecv&& data,
                                                         std::error_condition& /*errc*/) {
    Audio::MsgQueryAudioFileSend retData{};

    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }
    auto audioFiles = audio_svc_.QueryAudioFiles(retData.resData.totalCount, data.fileName,
                                                 data.keepFileTypes, data.pageNum, data.pageSize);
    for (auto& file : audioFiles) {
        Audio::MsgQueryAudioFileSend::AudioFile audioFile;
        audioFile.fileId    = file.id;
        audioFile.fileName  = file.name;
        audioFile.filePath  = audio_svc_.GetAudioFileWebPath(file);
        audioFile.timestamp = file.timestamp;
        retData.resData.audioFileList.push_back(audioFile);
    }

    return retData;
}

// Audio file delete
Audio::MsgDeleteAudioFileSend MessageAudioHandler::Handle(Audio::MsgDeleteAudioFileRecv&& data,
                                                          std::error_condition& /*errc*/) {
    Audio::MsgDeleteAudioFileSend retData;
    for (const auto& devId : data.fileIdList) {
        if (linkage_svc_.IsAudioFileInUse(devId)) {
            MsgResultInfo info;
            info.id      = devId;
            info.resCode = 1;
            info.resMsg  = "File In Using";
            retData.resData.failedList.push_back(info);
            continue;
        }
        MsgResultInfo info;
        info.id = devId;
        if (!audio_svc_.RemoveAudioFile(devId, info.resMsg)) {
            info.resCode = 2;
            retData.resData.failedList.push_back(info);
            continue;
        }
    }
    return retData;
}

// Audio pillar modify
Audio::MsgModifyAudioDeviceSend MessageAudioHandler::Handle(Audio::MsgModifyAudioDeviceRecv&& data,
                                                            std::error_condition& errc) {
    Audio::MsgModifyAudioDeviceSend retData;
    Operation ops = static_cast<Operation>(data.devOperation);
    switch (ops) {
        case Operation::Add: {
            if (data.audioDev.ip.empty()) {
                throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Audio pillar IP is empty");
            }

            if (!audio_svc_.AddAudioDevice(data.audioDev.name, data.audioDev.ip, data.audioDev.ethName,
                                           retData.resData.devId)) {
                throw util::ErrorMessage(util::ErrorEnum::Failed, "Audio pillar add failed");
            }
        } break;
        case Operation::Update:
            if (!audio_svc_.ModifyAudioDevice(data.audioDev.devId, data.audioDev.ip, data.audioDev.name,
                                              data.audioDev.ethName)) {
                throw util::ErrorMessage(util::ErrorEnum::Failed, "Audio pillar modify failed");
            }
            retData.resData.devId = data.audioDev.devId;
            break;
        default:
            errc = util::ErrorEnum::UnknownOperation;
            break;
    }
    return retData;
}

// Audio pillar query
Audio::MsgQueryAudioDeviceSend MessageAudioHandler::Handle(Audio::MsgQueryAudioDeviceRecv&& data,
                                                           std::error_condition& /*errc*/) {
    Audio::MsgQueryAudioDeviceSend retData{};

    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }
    auto audioDevs =
        audio_svc_.QueryAudioDevices(retData.resData.totalCount, data.name, data.pageNum, data.pageSize);
    std::map<std::string, std::future<bool>> ftrVec;
    for (auto& dev : audioDevs) {
        Audio::MsgQueryAudioDeviceSend::AudioDevice audioDev;
        audioDev.devId     = dev.id;
        audioDev.name      = dev.name;
        audioDev.ip        = dev.ip;
        audioDev.ethName   = dev.ethName;
        audioDev.timestamp = dev.timestamp;

        auto str = dev.ip;
        if (data.checkAlive) {
            auto it = ftrVec.find(str);
            if (it == ftrVec.end()) {
                ftrVec.emplace(str, std::async(std::launch::async, [this, str]() {
                                   auto ret = audio_svc_.CheckAudioDeviceAlive(str);
                                   LOG_INFO("{}:Online:{}", str, ret);
                                   return ret;
                               }));
            }
        }

        retData.resData.audioDevList.push_back(audioDev);
    }
    if (data.checkAlive) {
        for (auto& dev : retData.resData.audioDevList) {
            auto it = ftrVec.find(dev.ip);
            if (it != ftrVec.end()) {
                dev.online = it->second.get();
                LOG_INFO("{}:Online:{}", dev.ip, dev.online);
            }
        }
    }

    return retData;
}

// Audio pillar delete
Audio::MsgDeleteAudioDeviceSend MessageAudioHandler::Handle(Audio::MsgDeleteAudioDeviceRecv&& data,
                                                            std::error_condition& /*errc*/) {
    Audio::MsgDeleteAudioDeviceSend retData;
    for (const auto& devId : data.devIdList) {
        if (linkage_svc_.IsAudioDeviceInUse(devId)) {
            MsgResultInfo info;
            info.id      = devId;
            info.resCode = 1;
            info.resMsg  = "Device In Using";
            retData.resData.failedList.push_back(info);
            continue;
        }
        MsgResultInfo info;
        info.id = devId;
        if (!audio_svc_.RemoveAudioDevice(devId, info.resMsg)) {
            info.resCode = 2;
            retData.resData.failedList.push_back(info);
            continue;
        }
    }
    return retData;
}

// Audio pillar test
Audio::MsgTestAudioDeviceSend MessageAudioHandler::Handle(Audio::MsgTestAudioDeviceRecv&& data,
                                                          std::error_condition& /*errc*/) {
    Audio::MsgTestAudioDeviceSend retData;
    int operation = data.operation;
    AudioDevicePlay info;
    info.devId = data.devSn;
    info.data  = data.data;
    if (data.tone) {
        info.tone = AudioDeviceVoiceTone::AudioDeviceVoiceToneFemale;
    } else {
        info.tone = AudioDeviceVoiceTone::AudioDeviceVoiceToneMale;
    }
    info.volume   = data.volume;
    info.speed    = data.speed;
    info.duration = data.duration;
    info.times    = data.times;
    info.gap      = data.gap;
    if (1 == operation) {
        info.playType = AudioDevicePlayType::AudioDevicePlayTypeAudioPlay;
    } else if (2 == operation) {
        info.playType = AudioDevicePlayType::AudioDevicePlayTypeTextPlay;
    } else {
        throw util::ErrorMessage(util::ErrorEnum::Failed, "Audio pillar test failed");
    }

    if (!audio_svc_.PlayAudioDevice(info)) {
        throw util::ErrorMessage(util::ErrorEnum::Failed, "Audio pillar test failed");
    }

    return retData;
}
}  // namespace cosmo