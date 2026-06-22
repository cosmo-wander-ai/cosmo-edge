// FaceLib — Face Lib implementation.

#include "flow/face/FaceLib.h"

#include <future>

#include "flow/face/FacePic.h"
#include "flow/face/Person.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IFaceFeature.h"
#include "util/Exception.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace chrono = std::chrono;

FaceLib::FaceLib(const std::string & /*libId*/)
    : data_{std::make_unique<DataType>()}, create_time_{chrono::system_clock::now()} {
    LoadData();
}

FaceLib::FaceLib(DataType &&data)
    : data_{std::make_unique<DataType>(std::move(data))}, create_time_{chrono::system_clock::now()} {
    data_->id = util::GenerateUUID();
}

FaceLib::~FaceLib() {
    LOG_INFO("{}", data_->id);
    std::shared_lock<std::shared_mutex> lock(mtx_);
    SaveData();
}

const std::string &FaceLib::GetId() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return data_->id;
}

void FaceLib::AddFacePic(FacePicPtr pic) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = faces_.find(pic->GetId());
    if (it == faces_.end()) {
        faces_.emplace(pic->GetId(), pic);
        vec_faces_.push_back(pic);
    }
}

void FaceLib::RemoveFacePic(const std::string &faceId) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = faces_.find(faceId);
    if (it != faces_.end()) {
        faces_.erase(it);
    }
    auto itv = std::find_if(vec_faces_.begin(), vec_faces_.end(),
                            [&faceId](auto &&face) { return face->GetId() == faceId; });
    if (itv != vec_faces_.end()) {
        vec_faces_.erase(itv);
    }
}

size_t FaceLib::GetFaceCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return faces_.size();
}

size_t FaceLib::GetFaceMaxCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return data_->maxFaceNumber;
}

double FaceLib::GetThreshold() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return data_->threshold;
}

const std::string &FaceLib::GetName() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return data_->name;
}

int64_t FaceLib::GetCreateTime() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return chrono::duration_cast<chrono::milliseconds>(create_time_.time_since_epoch()).count();
}

void FaceLib::SetCreateTime(int64_t timestamp) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    create_time_ = chrono::system_clock::time_point(chrono::milliseconds(timestamp));
}

bool FaceLib::LoadData() {
    // To be implemented
    return true;
}

bool FaceLib::SaveData() {
    // To be implemented
    return true;
}

std::pair<FacePicPtr, float> FaceLib::SearchFeature(const AiFeature &feature) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    FacePicPtr most_similar;

    // Divide feature values into multiple parts, use multi-threading for comparison
    size_t division = std::thread::hardware_concurrency();
    while (faces_.size() / division < 100) {
        if (division == 1) {
            break;
        }
        division /= 2;
    }

    std::vector<std::future<std::pair<size_t, float>>> thr_cmp(division);
    size_t part_size = vec_faces_.size() / division + 1;
    for (size_t i = 0; i < division; ++i) {
        size_t beg_idx = part_size * i;
        size_t end_idx = std::min(part_size * (i + 1), vec_faces_.size());
        LOG_INFO("start: {}, end: {}", beg_idx, end_idx);
        thr_cmp[i] = std::async([beg_idx, end_idx, this, &feature]() {
            constexpr float kMinDistSentinel = 9999999.0f;
            float min_dist                   = kMinDistSentinel;
            size_t similar_idx               = vec_faces_.size();
            for (size_t j = beg_idx; j < end_idx; ++j) {
                auto dist =
                    service::ServiceRegistry::Instance().Get<service::IFaceFeature>().CalculateFaceScore(
                        vec_faces_[j]->GetFeature(), feature);
                if (dist < min_dist) {
                    similar_idx = j;
                    min_dist    = dist;
                }
            }
            return std::make_pair(similar_idx, min_dist);
        });
    }

    std::vector<std::pair<size_t, float>> dev_res;
    dev_res.reserve(thr_cmp.size());
    for (size_t i = 0; i < thr_cmp.size(); ++i) {
        auto res = thr_cmp[i].get();
        if (res.first < vec_faces_.size()) {
            dev_res.push_back(res);
        }
    }

    auto result = std::min_element(dev_res.begin(), dev_res.end(), [](const auto &res1, const auto &res2) {
        return res1.second < res2.second;
    });
    if (result != dev_res.end()) {
        return {
            vec_faces_[result->first],
            service::ServiceRegistry::Instance().Get<service::IFaceFeature>().GetFaceScore(result->second)};
    }
    return {nullptr, -1.0f};
}

const FaceLib::DataType &FaceLib::GetData() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return *data_;
}

void FaceLib::SetData(DataType &&data) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (data.name.empty()) {
        data.name = data_->name;
    }
    data_ = std::make_unique<DataType>(std::move(data));
}

size_t FaceLib::GetPersonCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::set<PersonPtr> persons;
    for (auto face : faces_) {
        if (auto person = face.second->GetPerson()) {
            persons.insert(person);
        }
    }
    return persons.size();
}

void FaceLib::GetAllPersons(std::set<std::string> &personIds) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto face : faces_) {
        if (auto person = face.second->GetPerson()) {
            personIds.insert(person->GetId());
        }
    }
}

}  // namespace cosmo
