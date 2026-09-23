#include <SQLiteCpp/SQLiteCpp.h>

#include <map>

#include "db/TaskEventDao.h"

namespace cosmo::db {
void TaskEventDao::CreateAttributeTables() {
    Db().exec(
        "CREATE TABLE IF NOT EXISTS t_attributeRecord (rec_id TEXT PRIMARY KEY, schema_id TEXT NOT NULL, "
        "schema_json TEXT NOT NULL)");
    Db().exec(
        "CREATE TABLE IF NOT EXISTS t_attributeValue (rec_id TEXT NOT NULL, attr_key TEXT NOT NULL, value "
        "TEXT NOT NULL, status TEXT NOT NULL, PRIMARY KEY(rec_id,attr_key,value))");
    Db().exec(
        "CREATE INDEX IF NOT EXISTS idx_attribute_lookup ON t_attributeValue(attr_key,value,status,rec_id)");
    Db().exec("CREATE INDEX IF NOT EXISTS idx_attribute_schema ON t_attributeRecord(schema_id,rec_id)");
    Db().exec(
        "CREATE INDEX IF NOT EXISTS idx_event_scene_channel_time ON "
        "t_commonEvent(algorithm_code,camera_id,create_time)");
    Db().exec(
        "CREATE TRIGGER IF NOT EXISTS delete_attribute_event AFTER DELETE ON t_commonEvent BEGIN DELETE FROM "
        "t_attributeValue WHERE rec_id=OLD.rec_id; DELETE FROM t_attributeRecord WHERE rec_id=OLD.rec_id; "
        "END");
}

void TaskEventDao::IndexAttributes(const std::string& id, const AttributeRecord& record) {
    SQLite::Statement schema(Db(), "INSERT INTO t_attributeRecord VALUES(?,?,?)");
    schema.bind(1, id);
    schema.bind(2, record.schemaId);
    schema.bind(3, nlohmann::json(record.schema).dump());
    schema.exec();
    SQLite::Statement value(Db(), "INSERT INTO t_attributeValue VALUES(?,?,?,?)");
    for (const auto& attribute : record.attributes) {
        const auto values = attribute.values.empty() ? std::vector<std::string>{""} : attribute.values;
        for (const auto& item : values) {
            value.reset();
            value.clearBindings();
            value.bind(1, id);
            value.bind(2, attribute.key);
            value.bind(3, item);
            value.bind(4, attribute.status);
            value.exec();
        }
    }
}

AttributeSummary TaskEventDao::QueryAttributeSummary(const QueryTaskEventCondition& condition) const {
    AttributeSummary result;
    auto scope = condition;
    scope.attribute_filters.clear();
    scope.attribute_schema_id.clear();
    const auto schema_conditions = BuildConditions(scope);
    SQLite::Statement schemas(Db(),
                              "SELECT DISTINCT ar.schema_id,ar.schema_json FROM t_attributeRecord ar WHERE "
                              "ar.rec_id IN (SELECT rec_id FROM t_commonEvent" +
                                  schema_conditions.BuildWhereClause() + ") ORDER BY ar.schema_id");
    schema_conditions.BindAll(schemas);
    while (schemas.executeStep()) {
        result.schemas.push_back({{"id", schemas.getColumn(0).getString()},
                                  {"schema", nlohmann::json::parse(schemas.getColumn(1).getString())}});
    }
    const auto cb      = BuildConditions(condition);
    const auto matched = "WITH matched AS (SELECT rec_id FROM t_commonEvent" + cb.BuildWhereClause() +
                         "), selected AS (SELECT ar.schema_id,av.* FROM t_attributeValue av JOIN matched m "
                         "ON m.rec_id=av.rec_id JOIN t_attributeRecord ar ON ar.rec_id=av.rec_id) ";
    SQLite::Statement counts(Db(),
                             matched +
                                 "SELECT schema_id,attr_key,value,status,COUNT(DISTINCT rec_id),"
                                 "(SELECT COUNT(DISTINCT v.rec_id) FROM selected v WHERE "
                                 "v.schema_id=s.schema_id AND v.attr_key=s.attr_key AND v.status='valid') "
                                 "FROM selected s GROUP BY schema_id,attr_key,value,status ORDER BY "
                                 "schema_id,attr_key,value,status");
    cb.BindAll(counts);
    while (counts.executeStep()) {
        AttributeStatistics row;
        row.schemaId   = counts.getColumn(0).getString();
        row.key        = counts.getColumn(1).getString();
        row.value      = counts.getColumn(2).getString();
        row.status     = counts.getColumn(3).getString();
        row.count      = counts.getColumn(4).getInt64();
        row.validCount = counts.getColumn(5).getInt64();
        result.statistics.push_back(std::move(row));
    }
    return result;
}
}  // namespace cosmo::db
