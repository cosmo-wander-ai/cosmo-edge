#pragma once
#include <system_error>

#include "service/face/dto/ThingsLibDto.h"

namespace cosmo::service {
class IArticlesReidDaoService;
class ICameraTaskConfig;
class IVideoFrameCodec;
}  // namespace cosmo::service

namespace cosmo {

class MessageThingsLibHandler {
public:
    MessageThingsLibHandler(service::IArticlesReidDaoService& dao_svc,
                            service::ICameraTaskConfig& camera_task_config,
                            service::IVideoFrameCodec& video_codec);
    ThingsLib::MsgModifyThingsLibSend Handle(ThingsLib::MsgModifyThingsLibRecv&& data,
                                             std::error_condition& errc);
    ThingsLib::MsgDeleteThingsLibSend Handle(ThingsLib::MsgDeleteThingsLibRecv&& data,
                                             std::error_condition& errc);
    ThingsLib::MsgQueryThingsLibInfoSend Handle(ThingsLib::MsgQueryThingsLibInfoRecv&& data,
                                                std::error_condition& errc);
    ThingsLib::MsgQueryThingsPicturesSend Handle(ThingsLib::MsgQueryThingsPicturesRecv&& data,
                                                 std::error_condition& errc);
    ThingsLib::MsgGetThingsPictureSend Handle(ThingsLib::MsgGetThingsPictureRecv&& data,
                                              std::error_condition& errc);
    ThingsLib::MsgAddLibThingsSend Handle(ThingsLib::MsgAddLibThingsRecv&& data, std::error_condition& errc);
    ThingsLib::MsgBindTaskThingsLibSend Handle(ThingsLib::MsgBindTaskThingsLibRecv&& data,
                                               std::error_condition& errc);
    ThingsLib::MsgDeleteLibThingsSend Handle(ThingsLib::MsgDeleteLibThingsRecv&& data,
                                             std::error_condition& errc);

private:
    service::IArticlesReidDaoService& dao_svc_;
    service::ICameraTaskConfig& camera_task_config_;
    service::IVideoFrameCodec& video_codec_;
};

}  // namespace cosmo
