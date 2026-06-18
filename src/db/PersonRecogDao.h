#pragma once

#include <string>
#include <vector>

#include "db/DaoBase.h"
#include "db/DbTypes.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {
enum class PersonRecogType {
    None        = 0,
    WorkClothes = 1,  // Work clothes
};

class PersonRecogDao : public DaoBase {
public:
    explicit PersonRecogDao(SQLite::Database& db);

    void CreateTable();

    // Person query
    [[nodiscard]] PersonRecogQueryResult QueryPersons(const PersonRecogQueryCondition& condition) const;
    // Person library query
    [[nodiscard]] PersonRecogLibQueryResult QueryPersonLib(
        const PersonRecogLibQueryCondition& condition) const;

    // Feature query
    [[nodiscard]] std::vector<float> QueryPersonFeature(const std::string& person_id) const;

    // Add person library
    bool AddPersonLib(const LibInfo& data);
    // Update person library
    bool UpdatePersonLib(const LibInfo& data);
    // Delete person library (including all persons)
    bool RemovePersonLib(const std::string& person_lib_id);

    // Clear person library (including all persons)
    bool ClearPersonLib(const std::string& person_lib_id);

    // Get all person libraries
    [[nodiscard]] std::vector<std::string> GetAllPersonLibs() const;

    // Add person
    bool AddPerson(const std::string& person_id, const std::string& person_lib_id,
                   const std::string& person_name, const std::vector<float>& feature);
    bool UpdatePerson(const std::string& person_id, const std::string& person_name);
    // Delete person
    bool RemovePerson(const std::string& person_id);

    // Clear all features
    bool ClearFeature();
    // Update feature
    bool UpdateFeature(const std::string& person_id, const std::vector<float>& feature);
};

}  // namespace cosmo::db
