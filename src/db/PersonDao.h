// PersonDao.h — Person and face library data access object.
//
// Implementation partitions (methods declared here, defined in separate .cc):
//   PersonDaoQuery.cc  — person and face library query operations

#pragma once

#include <string>
#include <vector>

#include "db/DaoBase.h"
#include "db/DbTypes.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {

class PersonDao : public DaoBase {
public:
    explicit PersonDao(SQLite::Database& db);

    void CreateTable();

    // Person query
    [[nodiscard]] FacePersonQueryResult QueryPersons(const FacePersonQueryCondition& condition) const;
    // Face library query
    [[nodiscard]] FaceLibQueryResult QueryFaceLib(const FaceLibQueryCondition& condition) const;
    // Feature query
    [[nodiscard]] std::vector<float> QueryFaceFeature(const std::string& face_id) const;

    // Add face library
    bool AddFaceLib(const LibInfo& data);
    // Update face library
    bool UpdateFaceLib(const LibInfo& data);
    // Delete face library (including all persons and faces)
    bool RemoveFaceLib(const std::string& face_lib_id);
    // Clear face library (including all persons and faces)
    bool ClearFaceLib(const std::string& face_lib_id);
    // Get all face libraries
    [[nodiscard]] std::vector<std::string> GetAllFaceLibs() const;

    // Add person
    bool AddPerson(FaceRegRecordUnit& face_reg);
    bool AddPerson(const PersonCondition& person,
                   const std::vector<std::pair<std::string, std::vector<float>>>& face);
    bool UpdatePerson(const PersonCondition& person,
                      const std::vector<std::pair<std::string, std::vector<float>>>& face);
    // Delete person
    bool RemovePerson(const std::string& person_id);
    // Remove relation
    bool RemoveFacesetPersonRelation(const std::string& person_id, const std::string& faceset_token);

private:
    bool AddPersonDetail(const FaceRegRecordUnit& person, int64_t time);
    bool AddPersonDetail(const PersonCondition& person,
                         const std::vector<std::pair<std::string, std::vector<float>>>& face, int64_t time);
    bool RemovePersonDetail(const std::string& person_id);
};

}  // namespace cosmo::db
