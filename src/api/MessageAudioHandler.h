#pragma once
#include <system_error>

#include "service/media/dto/AudioDto.h"

namespace cosmo::service {
class IAudioService;
class ILinkageService;
}  // namespace cosmo::service

namespace cosmo {

class MessageAudioHandler {
public:
    MessageAudioHandler(service::IAudioService& audio_svc, service::ILinkageService& linkage_svc);
    Audio::MsgQueryAudioFileSend Handle(Audio::MsgQueryAudioFileRecv&& data, std::error_condition& errc);
    Audio::MsgDeleteAudioFileSend Handle(Audio::MsgDeleteAudioFileRecv&& data, std::error_condition& errc);
    Audio::MsgModifyAudioDeviceSend Handle(Audio::MsgModifyAudioDeviceRecv&& data,
                                           std::error_condition& errc);
    Audio::MsgQueryAudioDeviceSend Handle(Audio::MsgQueryAudioDeviceRecv&& data, std::error_condition& errc);
    Audio::MsgDeleteAudioDeviceSend Handle(Audio::MsgDeleteAudioDeviceRecv&& data,
                                           std::error_condition& errc);
    Audio::MsgTestAudioDeviceSend Handle(Audio::MsgTestAudioDeviceRecv&& data, std::error_condition& errc);

private:
    service::IAudioService& audio_svc_;
    service::ILinkageService& linkage_svc_;
};

}  // namespace cosmo
