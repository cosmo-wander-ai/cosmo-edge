#pragma once

#include <string>
#include <vector>

#include "db/DaoBase.h"
#include "db/DbTypes.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {
enum class ArticlesReidType {
    None        = 0,
    WorkClothes = 1,  // Work clothes
};

class ArticlesReidDao : public DaoBase {
public:
    explicit ArticlesReidDao(SQLite::Database& db);

    void CreateTable();

    [[nodiscard]] ThingsQueryResult QueryThings(const ThingsQueryCondition& condition) const;
    [[nodiscard]] ThingsLibQueryResult QueryThingsLib(const ThingsLibQueryCondition& condition) const;

    // Feature query
    [[nodiscard]] std::vector<float> QueryArticlesReidFeature(const std::string& articles_reid_id) const;

    // Add things library
    bool AddArticlesReidLib(const LibInfo& data);
    // Update things library
    bool UpdateArticlesReidLib(const LibInfo& data);
    // Delete things library
    bool RemoveArticlesReidLib(const std::string& articles_reid_lib_id);

    // Clear things library
    bool ClearArticlesReidLib(const std::string& articles_reid_lib_id);

    // Get all things libraries
    [[nodiscard]] std::vector<std::string> GetAllArticlesReidLibs();

    bool AddArticlesReid(const std::string& articles_reid_id, const std::string& articles_reid_lib_id,
                         const std::string& articles_reid_name, const std::vector<float>& feature);
    bool UpdateArticlesReid(const std::string& articles_reid_id, const std::string& articles_reid_name);

    bool RemoveArticlesReid(const std::string& articles_reid_id);

    // Clear all features
    bool ClearFeature();
    // Update feature
    bool UpdateFeature(const std::string& articles_reid_id, const std::vector<float>& feature);
};

}  // namespace cosmo::db
