#pragma once

/*
 * Face library
 */

#include <chrono>
#include <map>
#include <set>
#include <shared_mutex>

#include "flow/face/FacePic.h"
#include "infer/AiCommon.h"
#include "infer/AiRecognizerUnify.h"
#include "util/LimitedType.h"
#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
namespace cosmo {
class FaceLib {
    friend class Person;

public:
    using DataType        = MsgBaseFaceLibInfo;
    using SystemTimePoint = std::chrono::system_clock::time_point;

    /*
     * Need to pass database operation object later for setting and updating data
     * If libId is not empty, read from database
     */
    explicit FaceLib(const std::string &libId = "");
    explicit FaceLib(DataType &&data);
    FaceLib(const FaceLib &)            = delete;
    FaceLib &operator=(const FaceLib &) = delete;
    ~FaceLib();

    // Face library name
    [[nodiscard]] const std::string &GetName() const;
    // Face library ID (internally generated or read from DB)
    [[nodiscard]] const std::string &GetId() const;
    // Face library size
    [[nodiscard]] size_t GetFaceCount() const;
    // Face library size
    [[nodiscard]] size_t GetPersonCount() const;
    // Get all persons
    void GetAllPersons(std::set<std::string> &personIds) const;
    // Max capacity
    [[nodiscard]] size_t GetFaceMaxCount() const;
    // Face library threshold
    [[nodiscard]] double GetThreshold() const;
    // Get creation time
    [[nodiscard]] int64_t GetCreateTime() const;
    // Set creation time
    void SetCreateTime(int64_t timestamp);

    // Get parameters
    [[nodiscard]] const DataType &GetData() const;
    // Set parameters
    void SetData(DataType &&data);

    // Search for most similar face
    [[nodiscard]] std::pair<FacePicPtr, float> SearchFeature(const AiFeature &feature) const;

private:
    // Load
    bool LoadData();
    // Save
    bool SaveData();
    // Add face
    void AddFacePic(FacePicPtr pic);
    // Delete face
    void RemoveFacePic(const std::string &faceId);

private:
    AiRecognizerUnifyPtr detect_reg_{nullptr};
    std::unique_ptr<DataType> data_;
    SystemTimePoint create_time_;
    std::map<std::string, FacePicPtr> faces_;  // Used for querying by ID
    std::vector<FacePicPtr> vec_faces_;        // Used for comparison
    mutable std::shared_mutex mtx_;
};

using FaceLibPtr  = std::shared_ptr<FaceLib>;
using FaceLibWPtr = std::weak_ptr<FaceLib>;

}  // namespace cosmo
