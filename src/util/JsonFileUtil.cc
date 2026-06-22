// JSON file read/write utility implementation.

#include "util/JsonFileUtil.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "util/FileUtil.h"
#include "util/JsonCompat.h"
#include "util/Log.h"

namespace cosmo::util {

cosmo::util::ErrorEnum JsonFileUtil::ReadJsonFile(const std::string& file_path, nlohmann::json& doc) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        LOG_WARN("Failed to open file: {}", file_path);
        return cosmo::util::ErrorEnum::FileNotExist;
    }
    try {
        doc = nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERRO("JSON parse error in file: {} — {}", file_path, e.what());
        return cosmo::util::ErrorEnum::Failed;
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum JsonFileUtil::WriteJsonFile(const std::string& file_path, const nlohmann::json& doc) {
    std::string dir_path = cosmo::util::GetParentPath(file_path);
    if (!dir_path.empty() && !cosmo::util::CreateDir(dir_path)) {
        LOG_ERRO("Failed to create directory: {}", dir_path);
        return cosmo::util::ErrorEnum::Failed;
    }
    std::ofstream ofs(file_path);
    if (!ofs.is_open()) {
        LOG_ERRO("Failed to open file for writing: {}", file_path);
        return cosmo::util::ErrorEnum::Failed;
    }
    ofs << doc.dump(2);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum JsonFileUtil::ReadJsonArray(const std::string& file_path, nlohmann::json& doc) {
    auto ret = ReadJsonFile(file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        if (ret == cosmo::util::ErrorEnum::FileNotExist) {
            doc = nlohmann::json::array();
            return cosmo::util::ErrorEnum::Success;
        }
        return ret;
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum JsonFileUtil::AppendToJsonArray(const std::string& file_path,
                                                       const nlohmann::json& item) {
    nlohmann::json doc;
    auto ret = ReadJsonArray(file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success)
        return ret;
    doc.push_back(item);
    return WriteJsonFile(file_path, doc);
}

cosmo::util::ErrorEnum JsonFileUtil::UpdateJsonArrayItem(const std::string& file_path, const std::string& key,
                                                         const std::string& value,
                                                         const nlohmann::json& new_item) {
    nlohmann::json doc;
    auto ret = ReadJsonArray(file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        doc = nlohmann::json::array();
    }
    nlohmann::json* found_item = FindItemInArray(doc, key, value);
    if (found_item) {
        *found_item = new_item;
    } else {
        doc.push_back(new_item);
    }
    return WriteJsonFile(file_path, doc);
}

cosmo::util::ErrorEnum JsonFileUtil::DeleteJsonArrayItem(const std::string& file_path, const std::string& key,
                                                         const std::string& value) {
    nlohmann::json doc;
    auto ret = ReadJsonArray(file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success)
        return ret;
    for (size_t i = 0; i < doc.size(); i++) {
        if (doc[i].contains(key) && doc[i][key].is_string() && doc[i][key].get<std::string>() == value) {
            doc.erase(doc.begin() + static_cast<nlohmann::json::difference_type>(i));
            return WriteJsonFile(file_path, doc);
        }
    }
    return cosmo::util::ErrorEnum::Failed;
}

nlohmann::json* JsonFileUtil::FindItemInArray(nlohmann::json& doc, const std::string& key,
                                              const std::string& value) {
    if (!doc.is_array())
        return nullptr;
    for (size_t i = 0; i < doc.size(); i++) {
        if (doc[i].contains(key) && doc[i][key].is_string() && doc[i][key].get<std::string>() == value)
            return &doc[i];
    }
    return nullptr;
}

}  // namespace cosmo::util
