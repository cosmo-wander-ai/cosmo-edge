// FaceLibService implementation

#pragma once

#include <memory>

#include "service/face/IFaceLibService.h"

namespace cosmo {
class FaceFeatureExtractor;
class PersonImport;
class FaceManager;
}  // namespace cosmo

namespace cosmo::service {

class FaceLibServiceImpl : public IFaceLibService {
public:
    FaceLibServiceImpl();
    ~FaceLibServiceImpl() override;

    // ---- FaceManager: face lib management ----
    std::vector<cosmo::FaceLibPtr> GetAllFaceLibs() override;
    size_t GetFaceLibMaxCount() const override;
    cosmo::util::ErrorEnum AddFaceLib(cosmo::FaceLibPtr faceLib) override;
    cosmo::util::ErrorEnum UpdateFaceLib(const std::string& faceLibId,
                                         cosmo::MsgBaseFaceLibInfo&& info) override;
    std::vector<cosmo::MsgResultFaceLibInfo> RemoveFaceLib(
        const std::vector<std::string>& libIdList) override;
    std::vector<cosmo::FaceLibPtr> GetFaceLibs(std::vector<std::string> libIds) const override;
    cosmo::FaceLibPtr GetFaceLib(const std::string& libId) const override;
    cosmo::FaceLibPtr CreateFaceLib(cosmo::MsgBaseFaceLibInfo&& data, std::string& outId) override;

    // ---- FaceManager: person management ----
    bool IsValidSerialNumber(const std::string& personId, const std::string& serialNumber) override;
    cosmo::PersonPtr GetPerson(const std::string& personId) const override;
    std::vector<cosmo::PersonPtr> GetAllPerson() const override;
    void AddPerson(cosmo::FaceLibPtr faceLib, cosmo::PersonPtr spPerson) override;
    void UpdatePerson(std::vector<cosmo::FaceLibPtr> faceLibList, cosmo::PersonPtr spPerson) override;
    std::string SetQueryCond(const cosmo::MsgQueryFacesR& queryCond) override;
    cosmo::util::ErrorEnum RemoveAllPerson(const std::string& faceLibId) override;
    std::vector<cosmo::MsgResultInfo> RemovePerson(cosmo::FaceLibPtr faceLib,
                                                   const std::vector<std::string>& personIdList) override;

    // ---- Person factory & mutation (IPersonRepo extensions) ----
    cosmo::PersonPtr CreatePerson() override;
    std::string GetPersonId(const cosmo::PersonPtr& person) const override;
    size_t GetPersonPictureCount(const cosmo::PersonPtr& person) const override;
    int64_t GetPersonCreateTime(const cosmo::PersonPtr& person) const override;
    std::vector<cosmo::FacePicPtr> GetPersonPictures(const cosmo::PersonPtr& person) const override;
    bool IsPersonInFaceLibs(const cosmo::PersonPtr& person,
                            const std::vector<std::string>& libIds) const override;
    void UpdatePersonMetadata(cosmo::PersonPtr person, const std::string& name,
                              const std::string& serialNumber, int64_t updateTime,
                              int64_t createTime) override;
    void AddPersonPicture(cosmo::PersonPtr person, cosmo::FacePicPtr pic) override;
    void RemovePersonPicture(cosmo::PersonPtr person, const std::string& picId) override;
    cosmo::FacePicPtr CreateFacePic(const std::string& id, const std::string& path,
                                    const cosmo::AiFeature& feature) override;

    // ---- Face feature ----
    cosmo::util::ErrorEnum ExtractFaceFeature(VideoFramePtr& image, float quality, cosmo::AiFeature& feature,
                                              VideoFramePtr& cutImage) override;
    float CalculateFaceScore(const cosmo::AiFeature& f1, const cosmo::AiFeature& f2) override;
    float GetFaceScore(float distance) override;
    bool FaceCompare(std::vector<std::string> sets, cosmo::AiFeature& feature,
                     cosmo::AiDetectMatchHighScoreInfo& info, float param_limit_score) override;
    void LoadFaceData() override;
    void ReleaseFaceModels() override;

    // ---- Person import ----
    void ImportFile(const std::string& filePath, const std::string& faceLibId) override;
    std::pair<int, int> GetImportStatus() const override;
    bool ImportComplete() const override;
    int GetImportTotalCount() const override;
    std::string GetImportFailedUrl() const override;

private:
    cosmo::FaceFeatureExtractor& EnsureFaceFeature();
    std::unique_ptr<cosmo::FaceFeatureExtractor> face_feature_;
    std::unique_ptr<cosmo::PersonImport> person_import_;
    std::unique_ptr<cosmo::FaceManager> face_manager_;
};

}  // namespace cosmo::service
