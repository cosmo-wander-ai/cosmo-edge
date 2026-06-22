#pragma once
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include "service/camera/dto/CameraDto.h"

namespace cosmo::service {
class ICameraDeviceCrud;
class ICameraChannelQuery;
class ICameraTaskConfig;
class ITaskQuery;
}  // namespace cosmo::service

namespace cosmo {

class MessageCameraHandler {
public:
    MessageCameraHandler(service::ICameraDeviceCrud& crud, service::ICameraChannelQuery& query,
                         service::ICameraTaskConfig& task_cfg, service::ITaskQuery& task_query);

    camera::MsgAddSend Handle(camera::MsgAddRecv&& data, std::error_condition& errc);
    camera::MsgUpdateSend Handle(camera::MsgUpdateRecv&& data, std::error_condition& errc);
    camera::MsgPageSend Handle(camera::MsgPageRecv&& data, std::error_condition& errc);
    camera::MsgDeleteSend Handle(camera::MsgDeleteRecv&& data, std::error_condition& errc);
    camera::MsgBatchDeleteSend Handle(camera::MsgBatchDeleteRecv&& data, std::error_condition& errc);
    camera::MsgAddVideoSend Handle(camera::MsgAddVideoRecv&& data, std::error_condition& errc);
    camera::MsgGetPictureSend Handle(camera::MsgGetPictureRecv&& data, std::error_condition& errc);
    camera::MsgQueryUsbCameraListSend Handle(camera::MsgQueryUsbCameraListRecv&& data,
                                             std::error_condition& errc);

private:
    // Phase 3: GetPicture helper methods (kept in Handler layer per design decision).
    std::string CaptureLiveFrame(const std::string& channel_id, std::error_condition& errc);
    void SaveChannelCache(const std::string& channel_id, const std::vector<uint8_t>& jpeg);
    std::string FallbackToCache(const std::string& channel_id, std::error_condition& errc);

    service::ICameraDeviceCrud& crud_;
    service::ICameraChannelQuery& query_;
    service::ICameraTaskConfig& task_cfg_;
    service::ITaskQuery& task_query_;
};

}  // namespace cosmo
