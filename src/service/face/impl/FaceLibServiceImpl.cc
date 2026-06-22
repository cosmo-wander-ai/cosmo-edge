// FaceLibServiceImpl — FaceLibService implementation

#include "service/face/impl/FaceLibServiceImpl.h"

#include "flow/face/FaceManager.h"
#include "flow/face/PersonImport.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/impl/FaceFeatureExtractor.h"
#include "util/Log.h"

namespace cosmo::service {

FaceLibServiceImpl::FaceLibServiceImpl()
    : person_import_(std::make_unique<cosmo::PersonImport>()),
      face_manager_(std::make_unique<cosmo::FaceManager>()) {}

cosmo::FaceFeatureExtractor& FaceLibServiceImpl::EnsureFaceFeature() {
    if (!face_feature_) {
        face_feature_ = std::make_unique<cosmo::FaceFeatureExtractor>();
    }
    return *face_feature_;
}

FaceLibServiceImpl::~FaceLibServiceImpl() = default;

// ---- FaceManager: face lib management ----
std::vector<cosmo::FaceLibPtr> FaceLibServiceImpl::GetAllFaceLibs() {
    return face_manager_->GetAllFaceLibs();
}

size_t FaceLibServiceImpl::GetFaceLibMaxCount() const {
    return face_manager_->GetFaceLibMaxCount();
}

cosmo::util::ErrorEnum FaceLibServiceImpl::AddFaceLib(cosmo::FaceLibPtr faceLib) {
    return face_manager_->AddFaceLib(faceLib);
}

cosmo::util::ErrorEnum FaceLibServiceImpl::UpdateFaceLib(const std::string& faceLibId,
                                                         cosmo::MsgBaseFaceLibInfo&& info) {
    return face_manager_->UpdateFaceLib(faceLibId, std::move(info));
}

std::vector<cosmo::MsgResultFaceLibInfo> FaceLibServiceImpl::RemoveFaceLib(
    const std::vector<std::string>& libIdList) {
    return face_manager_->RemoveFaceLib(libIdList);
}

std::vector<cosmo::FaceLibPtr> FaceLibServiceImpl::GetFaceLibs(std::vector<std::string> libIds) const {
    return face_manager_->GetFaceLibs(std::move(libIds));
}

cosmo::FaceLibPtr FaceLibServiceImpl::GetFaceLib(const std::string& libId) const {
    return face_manager_->GetFaceLib(libId);
}

cosmo::FaceLibPtr FaceLibServiceImpl::CreateFaceLib(cosmo::MsgBaseFaceLibInfo&& data, std::string& outId) {
    auto lib = std::make_shared<cosmo::FaceLib>(std::move(data));
    outId    = lib->GetId();
    return lib;
}

// ---- FaceManager: person management ----
bool FaceLibServiceImpl::IsValidSerialNumber(const std::string& personId, const std::string& serialNumber) {
    return face_manager_->IsValidSerialNumber(personId, serialNumber);
}

cosmo::PersonPtr FaceLibServiceImpl::GetPerson(const std::string& personId) const {
    return face_manager_->GetPerson(personId);
}

std::vector<cosmo::PersonPtr> FaceLibServiceImpl::GetAllPerson() const {
    return face_manager_->GetAllPerson();
}

void FaceLibServiceImpl::AddPerson(cosmo::FaceLibPtr faceLib, cosmo::PersonPtr spPerson) {
    face_manager_->AddPerson(std::move(faceLib), std::move(spPerson));
}

void FaceLibServiceImpl::UpdatePerson(std::vector<cosmo::FaceLibPtr> faceLibList, cosmo::PersonPtr spPerson) {
    face_manager_->UpdatePerson(std::move(faceLibList), std::move(spPerson));
}

std::string FaceLibServiceImpl::SetQueryCond(const cosmo::MsgQueryFacesR& queryCond) {
    return face_manager_->SetQueryCond(queryCond);
}

cosmo::util::ErrorEnum FaceLibServiceImpl::RemoveAllPerson(const std::string& faceLibId) {
    return face_manager_->RemoveAllPerson(faceLibId);
}

std::vector<cosmo::MsgResultInfo> FaceLibServiceImpl::RemovePerson(
    cosmo::FaceLibPtr faceLib, const std::vector<std::string>& personIdList) {
    return face_manager_->RemovePerson(std::move(faceLib), personIdList);
}

// ---- Person factory & mutation (IPersonRepo extensions) ----

cosmo::PersonPtr FaceLibServiceImpl::CreatePerson() {
    return std::make_shared<cosmo::Person>(std::string{});
}

std::string FaceLibServiceImpl::GetPersonId(const cosmo::PersonPtr& person) const {
    return person->GetId();
}

size_t FaceLibServiceImpl::GetPersonPictureCount(const cosmo::PersonPtr& person) const {
    return person->GetPictureCount();
}

int64_t FaceLibServiceImpl::GetPersonCreateTime(const cosmo::PersonPtr& person) const {
    return person->GetCreateTime();
}

std::vector<cosmo::FacePicPtr> FaceLibServiceImpl::GetPersonPictures(const cosmo::PersonPtr& person) const {
    return person->GetPictures();
}

bool FaceLibServiceImpl::IsPersonInFaceLibs(const cosmo::PersonPtr& person,
                                            const std::vector<std::string>& libIds) const {
    return person->IsInFaceLibs(libIds);
}

void FaceLibServiceImpl::UpdatePersonMetadata(cosmo::PersonPtr person, const std::string& name,
                                              const std::string& serialNumber, int64_t updateTime,
                                              int64_t createTime) {
    if (!name.empty()) {
        person->SetName(name);
    }
    if (!serialNumber.empty()) {
        person->SetSerialNumber(serialNumber);
    }
    person->SetUpdateTime(updateTime);
    if (person->GetCreateTime() == 0) {
        person->SetCreateTime(createTime);
    }
}

void FaceLibServiceImpl::AddPersonPicture(cosmo::PersonPtr person, cosmo::FacePicPtr pic) {
    person->AddPicture(std::move(pic));
}

void FaceLibServiceImpl::RemovePersonPicture(cosmo::PersonPtr person, const std::string& picId) {
    person->RemovePicture(picId);
}

cosmo::FacePicPtr FaceLibServiceImpl::CreateFacePic(const std::string& id, const std::string& path,
                                                    const cosmo::AiFeature& feature) {
    return std::make_shared<cosmo::FacePic>(id, path, feature);
}

// ---- Face feature ----

cosmo::util::ErrorEnum FaceLibServiceImpl::ExtractFaceFeature(VideoFramePtr& image, float quality,
                                                              cosmo::AiFeature& feature,
                                                              VideoFramePtr& cutImage) {
    auto ec = EnsureFaceFeature().HandFeatureImage(image, quality, feature, cutImage);
    return static_cast<cosmo::util::ErrorEnum>(ec.value());
}

float FaceLibServiceImpl::CalculateFaceScore(const cosmo::AiFeature& f1, const cosmo::AiFeature& f2) {
    return EnsureFaceFeature().CalculateScore(f1, f2);
}

float FaceLibServiceImpl::GetFaceScore(float distance) {
    return EnsureFaceFeature().GetScore(distance);
}

bool FaceLibServiceImpl::FaceCompare(std::vector<std::string> sets, cosmo::AiFeature& feature,
                                     cosmo::AiDetectMatchHighScoreInfo& info, float param_limit_score) {
    return face_manager_->FaceCompare(std::move(sets), feature, info, param_limit_score);
}

void FaceLibServiceImpl::LoadFaceData() {
    face_manager_->Load();
}

void FaceLibServiceImpl::ReleaseFaceModels() {
    if (face_feature_) {
        LOG_INFO("{}", "FaceLibServiceImpl: releasing face feature models to free VRAM");
        face_feature_.reset();
    }
}

// ---- Person import ----
void FaceLibServiceImpl::ImportFile(const std::string& filePath, const std::string& faceLibId) {
    person_import_->ImportFile(filePath, faceLibId);
}

std::pair<int, int> FaceLibServiceImpl::GetImportStatus() const {
    return person_import_->GetStatus();
}

bool FaceLibServiceImpl::ImportComplete() const {
    return person_import_->Complete();
}

int FaceLibServiceImpl::GetImportTotalCount() const {
    return person_import_->GetTotalCount();
}

std::string FaceLibServiceImpl::GetImportFailedUrl() const {
    return person_import_->GetFailedUrl();
}

}  // namespace cosmo::service
