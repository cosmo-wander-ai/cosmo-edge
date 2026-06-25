#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/face/IFaceLibService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockFaceLibService : public cosmo::service::IFaceLibService {
public:
    // IFaceLibRepo
    MAKE_MOCK0(GetAllFaceLibs, std::vector<cosmo::FaceLibPtr>(), override);
    MAKE_CONST_MOCK0(GetFaceLibMaxCount, size_t(), override);
    MAKE_MOCK1(AddFaceLib, cosmo::util::ErrorEnum(cosmo::FaceLibPtr), override);
    MAKE_MOCK2(UpdateFaceLib, cosmo::util::ErrorEnum(const std::string&, cosmo::MsgBaseFaceLibInfo&&),
               override);
    MAKE_MOCK1(RemoveFaceLib, std::vector<cosmo::MsgResultFaceLibInfo>(const std::vector<std::string>&),
               override);
    MAKE_CONST_MOCK1(GetFaceLibs, std::vector<cosmo::FaceLibPtr>(std::vector<std::string>), override);
    MAKE_CONST_MOCK1(GetFaceLib, cosmo::FaceLibPtr(const std::string&), override);
    MAKE_MOCK2(CreateFaceLib, cosmo::FaceLibPtr(cosmo::MsgBaseFaceLibInfo&&, std::string&), override);
    // IPersonRepo
    MAKE_MOCK2(IsValidSerialNumber, bool(const std::string&, const std::string&), override);
    MAKE_CONST_MOCK1(GetPerson, cosmo::PersonPtr(const std::string&), override);
    MAKE_CONST_MOCK0(GetAllPerson, std::vector<cosmo::PersonPtr>(), override);
    MAKE_MOCK2(AddPerson, void(cosmo::FaceLibPtr, cosmo::PersonPtr), override);
    MAKE_MOCK2(UpdatePerson, void(std::vector<cosmo::FaceLibPtr>, cosmo::PersonPtr), override);
    MAKE_MOCK1(RemoveAllPerson, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK2(RemovePerson,
               std::vector<cosmo::MsgResultInfo>(cosmo::FaceLibPtr, const std::vector<std::string>&),
               override);
    MAKE_MOCK0(CreatePerson, cosmo::PersonPtr(), override);
    MAKE_CONST_MOCK1(GetPersonId, std::string(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonPictureCount, size_t(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonCreateTime, int64_t(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonPictures, std::vector<cosmo::FacePicPtr>(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK2(IsPersonInFaceLibs, bool(const cosmo::PersonPtr&, const std::vector<std::string>&),
                     override);
    MAKE_MOCK5(UpdatePersonMetadata,
               void(cosmo::PersonPtr, const std::string&, const std::string&, int64_t, int64_t), override);
    MAKE_MOCK2(AddPersonPicture, void(cosmo::PersonPtr, cosmo::FacePicPtr), override);
    MAKE_MOCK2(RemovePersonPicture, void(cosmo::PersonPtr, const std::string&), override);
    MAKE_MOCK3(CreateFacePic,
               cosmo::FacePicPtr(const std::string&, const std::string&, const cosmo::AiFeature&), override);
    // IFaceFeature
    MAKE_MOCK4(ExtractFaceFeature,
               cosmo::util::ErrorEnum(VideoFramePtr&, float, cosmo::AiFeature&, VideoFramePtr&), override);
    MAKE_MOCK2(CalculateFaceScore, float(const cosmo::AiFeature&, const cosmo::AiFeature&), override);
    MAKE_MOCK1(GetFaceScore, float(float), override);
    MAKE_MOCK4(FaceCompare,
               bool(std::vector<std::string>, cosmo::AiFeature&, cosmo::AiDetectMatchHighScoreInfo&, float),
               override);
    MAKE_MOCK0(LoadFaceData, void(), override);
    MAKE_MOCK0(ReleaseFaceModels, void(), override);
    // IFaceImport
    MAKE_MOCK2(ImportFile, void(const std::string&, const std::string&), override);
    MAKE_CONST_MOCK0(GetImportStatus, (std::pair<int, int>)(), override);
    MAKE_CONST_MOCK0(ImportComplete, bool(), override);
    MAKE_CONST_MOCK0(GetImportTotalCount, int(), override);
    MAKE_CONST_MOCK0(GetImportFailedUrl, std::string(), override);
    // IFaceLibService own method
    MAKE_MOCK1(SetQueryCond, std::string(const cosmo::MsgQueryFacesR&), override);
};

}  // namespace cosmo::test
