#pragma once

/*
 * Face library management,
 * All face library, person, and face info are loaded into memory
 * Person can be changed to real-time loading from database
 */

#include <functional>
#include <iosfwd>
#include <shared_mutex>

#include "flow/face/FaceLib.h"
#include "flow/face/Person.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {
struct MsgQueryFacesSendPerson : public MsgQueryFacesS::Person {
    explicit MsgQueryFacesSendPerson(const MsgQueryFacesS::Person& person) : MsgQueryFacesS::Person(person) {}
};

/// Face library manager — manages face libraries, persons and face pictures.
/// Owned by FaceLibServiceImpl (no longer a singleton).
class FaceManager {
public:
    // detectLibGen: function used to generate detection library (see FaceLib)
    FaceManager()  = default;
    ~FaceManager() = default;

    // Get face library
    [[nodiscard]] std::vector<FaceLibPtr> GetFaceLibs(std::vector<std::string> lib_ids) const;
    // Check if name corresponding to ID is available
    [[nodiscard]] bool IsValidLibName(const std::string& face_lib_id, const std::string& face_lib_name) const;
    // Add face library
    util::ErrorEnum AddFaceLib(FaceLibPtr face_lib);
    // Update face library info
    util::ErrorEnum UpdateFaceLib(const std::string& face_lib_id, MsgBaseFaceLibInfo&& face_lib_info);
    // Delete face library
    util::ErrorEnum RemoveFaceLib(const std::string& lib_id);
    std::vector<MsgResultFaceLibInfo> RemoveFaceLib(const std::vector<std::string>& lib_id_list);

    [[nodiscard]] FaceLibPtr GetFaceLib(const std::string& lib_id) const;
    // Get all face libraries
    [[nodiscard]] std::vector<FaceLibPtr> GetAllFaceLibs() const;

    // Max number of face libraries
    [[nodiscard]] size_t GetFaceLibMaxCount() const;

    // Get single person info
    [[nodiscard]] PersonPtr GetPerson(const std::string& person_id) const;
    // Get all person info
    [[nodiscard]] std::vector<PersonPtr> GetAllPerson() const;
    // Add person
    void AddPerson(FaceLibPtr face_lib, PersonPtr person);
    // Update person (delete then add)
    void UpdatePerson(std::vector<FaceLibPtr> face_lib_list, PersonPtr person);
    // Check if there is a case with different personId but same department and number
    [[nodiscard]] bool IsValidSerialNumber(const std::string& person_id, const std::string& serial_number);
    // Delete person (if face library is empty, applies to all libraries)
    util::ErrorEnum RemovePerson(FaceLibPtr face_lib, const std::string& person_id);
    std::vector<MsgResultInfo> RemovePerson(FaceLibPtr face_lib,
                                            const std::vector<std::string>& person_id_list);
    // Clear persons in face library
    util::ErrorEnum RemoveAllPerson(const std::string& face_lib_id);
    // Save query condition, return query ID
    std::string SetQueryCond(const MsgQueryFacesR& query_cond);
    // Save person query results
    util::ErrorEnum ExportPersonToPath(const std::string& query_id,
                                       const std::vector<std::string>& person_id_list,
                                       const std::string& path);

    // Data loading (load once, save in real-time)
    void Load();

    // Extract features and set image as face image
    //  std::vector<float> ExtractFeature(UnifiedImage &facePicb);

    [[nodiscard]] bool FaceCompare(std::vector<std::string> sets, const AiFeature& feature,
                                   AiDetectMatchHighScoreInfo& info, float param_limit_score);

private:
    // Export implementation
    std::ostream& WriteHeader(std::ostream&);
    std::ostream& WritePersonData(std::ostream&, size_t index, const MsgQueryFacesSendPerson& res_person,
                                  const std::string& http_dir);

private:
    std::map<std::string, PersonPtr> person_map_;  // All persons indexed by ID for search
    std::vector<FaceLibPtr> face_libs_;            // All face libraries
    size_t max_face_number_{300000};
    size_t max_face_lib_number_{100};
    std::pair<std::string, std::unique_ptr<MsgQueryFacesR>> query_cond_;
    mutable std::shared_mutex mtx_;
};
}  // namespace cosmo
