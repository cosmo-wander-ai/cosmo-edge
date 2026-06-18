// PersonDaoQuery.cc — Person query operations for PersonDao.
// Split from PersonDao.cc to reduce file size (DEBT-007).

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>

#include "db/PersonDao.h"
#include "db/RowFieldReader.h"
#include "util/DurationLogger.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::db {

bool PersonDao::UpdateFaceLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(), "update t_faceset set alias=?,capacity=?,threshold=?,modify_time=? where faceset_token=?");
    stmt.bind(1, data.name);
    stmt.bind(2, std::to_string(data.max_capacity));
    stmt.bind(3, std::to_string(data.threshold));
    stmt.bind(4, static_cast<int64_t>(now_time));
    stmt.bind(5, data.id);

    return stmt.exec() > 0;
}

bool PersonDao::RemoveFaceLib(const std::string& face_lib_id) {
    // Clear face library data
    ClearFaceLib(face_lib_id);
    // Delete face library
    SQLite::Statement stmt(Db(), "DELETE FROM t_faceset WHERE faceset_token=?");
    stmt.bind(1, face_lib_id);
    stmt.exec();
    return true;
}

bool PersonDao::ClearFaceLib(const std::string& face_lib_id) {
    // Query person IDs that belong only to this face library (relation count <= 1)
    std::vector<std::string> person_ids_to_remove;
    {
        SQLite::Statement queryPersons(Db(),
                                       "SELECT fsp.person_id FROM t_faceset_person_relation fsp "
                                       "WHERE fsp.faceset_token=? AND "
                                       "(SELECT count(faceset_token) FROM t_faceset_person_relation "
                                       " WHERE person_id=fsp.person_id) <= 1");
        queryPersons.bind(1, face_lib_id);
        while (queryPersons.executeStep()) {
            person_ids_to_remove.push_back(queryPersons.getColumn(0).getString());
        }
    }

    // Delete face and person records
    for (const auto& pid : person_ids_to_remove) {
        SQLite::Statement delFace(Db(), "DELETE FROM t_face WHERE person_id=?");
        delFace.bind(1, pid);
        delFace.exec();

        SQLite::Statement delPerson(Db(), "DELETE FROM t_person WHERE person_id=?");
        delPerson.bind(1, pid);
        delPerson.exec();
    }

    // Delete relation records
    SQLite::Statement delRelation(Db(), "DELETE FROM t_faceset_person_relation WHERE faceset_token=?");
    delRelation.bind(1, face_lib_id);
    delRelation.exec();

    return true;
}

bool PersonDao::AddPerson(FaceRegRecordUnit& face_reg) {
    auto now_time    = util::GetMilliseconds();
    auto update_time = face_reg.face_update_time;
    auto create_time = face_reg.face_create_time;

    bool success = true;

    try {
        SQLite::Statement stmt(Db(),
                               "insert into t_person ("
                               "person_id, name, ic_card, id_card, modify_time, create_time"
                               ") values (?,?,?,?,?,?)");
        stmt.bind(1, face_reg.id);
        stmt.bind(2, face_reg.face_name);
        stmt.bind(3, face_reg.serial_name);
        stmt.bind(4, "");
        stmt.bind(5, (0 == update_time) ? static_cast<int64_t>(now_time) : update_time);
        stmt.bind(6, (0 == create_time) ? static_cast<int64_t>(now_time) : create_time);

        int affected_rows = stmt.exec();
        if (affected_rows <= 0) {
            LOG_WARN("{}", "PersonDao::AddPerson failed");
        }
        success = affected_rows && success;
    } catch (const std::exception& e) {
        LOG_INFO("PersonDao::AddPerson error {}", e.what());
    }

    return success && AddPersonDetail(face_reg, now_time);
}

bool PersonDao::AddPerson(const PersonCondition& person,
                          const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    auto now_time    = util::GetMilliseconds();
    auto update_time = person.update_time;
    auto create_time = person.create_time;

    bool success = true;

    SQLite::Statement stmt(Db(),
                           "insert into t_person ("
                           "person_id, name, ic_card, id_card, modify_time, create_time"
                           ") values (?,?,?,?,?,?)");
    stmt.bind(1, person.person_id);
    stmt.bind(2, person.person_name);
    stmt.bind(3, person.serial_number);
    stmt.bind(4, "");
    stmt.bind(5, (0 == update_time) ? static_cast<int64_t>(now_time) : update_time);
    stmt.bind(6, (0 == create_time) ? static_cast<int64_t>(now_time) : create_time);

    success = stmt.exec() > 0 && success;

    return success && AddPersonDetail(person, face, now_time);
}

bool PersonDao::UpdatePerson(const PersonCondition& person,
                             const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    RemovePersonDetail(person.person_id);

    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(Db(),
                           "update t_person set name=?,ic_card=?,id_card=?,modify_time=? where person_id=?");
    stmt.bind(1, person.person_name);
    stmt.bind(2, person.serial_number);
    stmt.bind(3, "");
    stmt.bind(4, (0 == person.update_time) ? static_cast<int64_t>(now_time) : person.update_time);
    stmt.bind(5, person.person_id);
    stmt.exec();

    return AddPersonDetail(person, face, now_time);
}

bool PersonDao::AddPersonDetail(const FaceRegRecordUnit& person, int64_t time) {
    // Insert face library-person relation
    for (auto& face_lib_id : person.face_lib_id) {
        SQLite::Statement stmt(Db(),
                               "insert into t_faceset_person_relation ("
                               "faceset_token, person_id, modify_time, create_time"
                               ") values (?,?,?,?)");
        stmt.bind(1, face_lib_id);
        stmt.bind(2, person.id);
        stmt.bind(3, time);
        stmt.bind(4, time);

        if (stmt.exec() <= 0) {
            return false;
        }
    }

    // Insert face records
    for (auto& pFacePic : person.face_pic_infos) {
        SQLite::Statement stmt(Db(),
                               "insert into t_face ("
                               "face_token, person_id, face_feature, img_url, modify_time, create_time"
                               ") values (?,?,?,?,?,?)");
        stmt.bind(1, pFacePic.id);
        stmt.bind(2, person.id);
        stmt.bind(3, reinterpret_cast<const void*>(pFacePic.feature.data()),
                  static_cast<int>(pFacePic.feature.size() * 4));
        stmt.bind(4, pFacePic.id);
        stmt.bind(5, time);
        stmt.bind(6, time);

        if (stmt.exec() <= 0) {
            return false;
        }
    }

    return true;
}

bool PersonDao::AddPersonDetail(const PersonCondition& person,
                                const std::vector<std::pair<std::string, std::vector<float>>>& face,
                                int64_t time) {
    // Insert face library-person relation
    for (auto& face_lib_id : person.face_lib_id) {
        SQLite::Statement stmt(Db(),
                               "insert into t_faceset_person_relation ("
                               "faceset_token, person_id, modify_time, create_time"
                               ") values (?,?,?,?)");
        stmt.bind(1, face_lib_id);
        stmt.bind(2, person.person_id);
        stmt.bind(3, time);
        stmt.bind(4, time);

        if (stmt.exec() <= 0) {
            return false;
        }
    }

    // Insert face records
    for (auto& f : face) {
        SQLite::Statement stmt(Db(),
                               "insert into t_face ("
                               "face_token, person_id, face_feature, img_url, modify_time, create_time"
                               ") values (?,?,?,?,?,?)");
        stmt.bind(1, f.first);
        stmt.bind(2, person.person_id);
        stmt.bind(3, reinterpret_cast<const void*>(f.second.data()), static_cast<int>(f.second.size() * 4));
        stmt.bind(4, f.first);
        stmt.bind(5, time);
        stmt.bind(6, time);

        if (stmt.exec() <= 0) {
            return false;
        }
    }

    return true;
}

bool PersonDao::RemovePerson(const std::string& person_id) {
    bool success = true;

    // Delete person record
    SQLite::Statement stmt(Db(), "DELETE FROM t_person WHERE person_id=?");
    stmt.bind(1, person_id);
    success = stmt.exec() > 0 && success;

    return success && RemovePersonDetail(person_id);
}

bool PersonDao::RemovePersonDetail(const std::string& person_id) {
    bool success = true;

    // Delete face library-person relation
    SQLite::Statement delRelation(Db(), "DELETE FROM t_faceset_person_relation WHERE person_id=?");
    delRelation.bind(1, person_id);
    success = delRelation.exec() > 0 && success;

    // Delete face records
    SQLite::Statement delFace(Db(), "DELETE FROM t_face WHERE person_id=?");
    delFace.bind(1, person_id);
    success = delFace.exec() > 0 && success;

    return success;
}

bool PersonDao::RemoveFacesetPersonRelation(const std::string& person_id, const std::string& faceset_token) {
    bool success = true;

    // Delete face library-person relation
    SQLite::Statement stmt(Db(),
                           "DELETE FROM t_faceset_person_relation WHERE person_id=? AND faceset_token=?");
    stmt.bind(1, person_id);
    stmt.bind(2, faceset_token);
    success = stmt.exec() > 0 && success;

    return success;
}

std::vector<std::string> PersonDao::GetAllFaceLibs() const {
    std::vector<std::string> result;
    // Query statement
    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append("select faceset_token from t_faceset");

    SQLite::Statement query(Db(), query_sql);
    while (query.executeStep()) {
        std::string faceset_token;

        RowFieldReader row(query);
        row.SetValue(faceset_token);
        result.emplace_back(std::move(faceset_token));
    }

    return result;
}

}  // namespace cosmo::db
