#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace cosmo::util {

enum class FileAttr {
    FileAttrFile = 0,  // Regular file
    FileAttrDir        // Directory
};

constexpr size_t kDefaultMaxFileSize    = 128u * 1024 * 1024;
constexpr size_t kDefaultMaxBinFileSize = 1024 * 1024 * 1024;

time_t GetFileModifyTime(const std::string& file_name);
bool FileExist(const std::string& file_path);
size_t GetFileSize(const std::string& file_name);

std::string ReadFile(const std::string& file_name, size_t max_size = kDefaultMaxFileSize);
std::vector<uint8_t> ReadFileBin(const std::string& file_name, size_t max_size = kDefaultMaxBinFileSize);

bool WriteFile(const std::string& file_name, const std::string& data);
bool WriteFile(const std::string& file_name, const std::uint8_t* data, int size);

bool WriteFileAppend(const std::string& file_name, const std::string& data);
bool WriteFileAppend(const std::string& file_name, const std::uint8_t* data, int size);

std::vector<std::string> GetSubDir(const std::string& base_dir, bool add_base = false);

bool CreateDir(const std::string& path, bool clear = false);

std::vector<std::string> GetAllFileName(const std::string& dir, const std::string& filter = "",
                                        FileAttr file_type = FileAttr::FileAttrFile);

std::string BaseSimplify(const std::string& path);

std::string GetParentPath(const std::string& file_name);

std::string GetFileName(const std::string& file_name);

std::string RemoveExtension(const std::string& file_path);

bool FileMove(const std::string& file_name, const std::string& path);
bool FileMoveWithRename(const std::string& file_name, const std::string& new_file);

bool RemovePath(const std::string& path);

bool RemoveFile(const std::string& file_name);

std::string FindPrefixedJsonFile(const std::string& dir, const std::string& prefix);

}  // namespace cosmo::util
