#pragma once
#include <system_error>

#include "service/face/dto/BodyLibDto.h"

namespace cosmo::service {
class IPersonRecogDaoService;
class IBodyLibService;
class ICameraTaskConfig;
class IVideoFrameCodec;
}  // namespace cosmo::service

namespace cosmo {

class MessageBodyLibHandler {
public:
    MessageBodyLibHandler(service::IPersonRecogDaoService& dao_svc, service::IBodyLibService& body_lib_svc,
                          service::ICameraTaskConfig& camera_task_config,
                          service::IVideoFrameCodec& video_codec);

    [[nodiscard]] BodyLib::MsgModifyPersonLibSend Handle(BodyLib::MsgModifyPersonLibRecv&& data,
                                                         std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgDeletePersonLibSend Handle(BodyLib::MsgDeletePersonLibRecv&& data,
                                                         std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgQueryPersonLibInfoSend Handle(BodyLib::MsgQueryPersonLibInfoRecv&& data,
                                                            std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgQueryPersonPicturesSend Handle(BodyLib::MsgQueryPersonPicturesRecv&& data,
                                                             std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgAddLibPersonSend Handle(BodyLib::MsgAddLibPersonRecv&& data,
                                                      std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgBindTaskPersonLibSend Handle(BodyLib::MsgBindTaskPersonLibRecv&& data,
                                                           std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgDeleteLibPersonSend Handle(BodyLib::MsgDeleteLibPersonRecv&& data,
                                                         std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgDetectPersonSend Handle(BodyLib::MsgDetectPersonRecv&& data,
                                                      std::error_condition& errc);
    [[nodiscard]] BodyLib::MsgGetPersonPictureSend Handle(BodyLib::MsgGetPersonPictureRecv&& data,
                                                          std::error_condition& errc);

private:
    /// Invalidate body library cache after modification.
    void InvalidateBodyCache(const std::string& lib_id);

    service::IPersonRecogDaoService& dao_svc_;
    service::IBodyLibService& body_lib_svc_;
    service::ICameraTaskConfig& camera_task_config_;
    service::IVideoFrameCodec& video_codec_;
};

}  // namespace cosmo
