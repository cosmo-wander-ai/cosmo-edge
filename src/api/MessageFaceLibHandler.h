#pragma once

#include <system_error>

#include "service/face/dto/FaceLibDto.h"
#include "service/face/dto/FaceLibFwd.h"

namespace cosmo::service {
class IFaceLibRepo;
class IPersonRepo;
class IFaceFeature;
class IPersonDaoService;
class IFaceLibService;
class IVideoFrameCodec;
}  // namespace cosmo::service

namespace cosmo {

class MessageFaceLibHandler {
public:
    MessageFaceLibHandler(service::IFaceLibRepo& lib_repo, service::IPersonRepo& person_repo,
                          service::IFaceFeature& face_feature, service::IPersonDaoService& dao_svc,
                          service::IFaceLibService& face_lib_svc, service::IVideoFrameCodec& codec);

    [[nodiscard]] Lib::MsgModifyFaceLibSend Handle(Lib::MsgModifyFaceLibRecv&& data,
                                                   std::error_condition& errc);
    [[nodiscard]] Lib::MsgModifyFacePicLibSend Handle(Lib::MsgModifyFacePicLibRecv&& data,
                                                      std::error_condition& errc);
    [[nodiscard]] Lib::MsgQueryFaceLibInfoSend Handle(Lib::MsgQueryFaceLibInfoRecv&& data,
                                                      std::error_condition& errc);
    [[nodiscard]] Lib::MsgDeleteFaceLibSend Handle(Lib::MsgDeleteFaceLibRecv&& data,
                                                   std::error_condition& errc);
    [[nodiscard]] Lib::MsgQueryFacesSend Handle(Lib::MsgQueryFacesRecv&& data, std::error_condition& errc);
    [[nodiscard]] Lib::MsgDeletePersonSend Handle(Lib::MsgDeletePersonRecv&& data,
                                                  std::error_condition& errc);

private:
    void HandleModifyFacePicUpdate(Lib::MsgModifyFacePicLibRecv& data, std::error_condition& errc,
                                   Lib::MsgModifyFacePicLibSend& retData, std::vector<FaceLibPtr>& faceLibs,
                                   PersonPtr& person);

    void ValidateFaceLibCapacity(const std::vector<FaceLibPtr>& faceLibs, const PersonPtr& person,
                                 const std::vector<FacePicPtr>& vFacePicNow,
                                 const Lib::MsgModifyFacePicLibRecv& data);

    void ProcessNewPictures(const Lib::MsgModifyFacePicLibRecv& data, std::error_condition& errc,
                            Lib::MsgModifyFacePicLibSend& retData,
                            std::vector<std::pair<std::string, std::vector<float>>>& faceFeature,
                            std::vector<FacePicPtr>& vFacePic);

    void CollectRetainedPictures(const std::vector<FacePicPtr>& vFacePicNow,
                                 const std::vector<std::string>& retainPictureId,
                                 std::vector<std::pair<std::string, std::vector<float>>>& faceFeature,
                                 std::vector<FacePicPtr>& vFacePic, std::vector<std::string>& vRemovePicId);

    void CommitPersonChanges(Lib::MsgModifyFacePicLibRecv& data, std::vector<FaceLibPtr>& faceLibs,
                             PersonPtr& person,
                             const std::vector<std::pair<std::string, std::vector<float>>>& faceFeature,
                             const std::vector<FacePicPtr>& vFacePic,
                             const std::vector<std::string>& vRemovePicId,
                             Lib::MsgModifyFacePicLibSend& retData);

    service::IFaceLibRepo& lib_repo_;
    service::IPersonRepo& person_repo_;
    service::IFaceFeature& face_feature_;
    service::IPersonDaoService& dao_svc_;
    service::IFaceLibService& face_lib_svc_;
    service::IVideoFrameCodec& codec_;
};

}  // namespace cosmo
