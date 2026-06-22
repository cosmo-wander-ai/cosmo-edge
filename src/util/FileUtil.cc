// FileUtil — File Util implementation.

#include "util/FileUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "util/Log.h"

namespace fs = std::filesystem;

using FilePtr = std::unique_ptr<FILE, decltype(&fclose)>;

namespace cosmo::util {

inline time_t ToTimeT(fs::file_time_type tp) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
}

time_t GetFileModifyTime(const std::string& file_name) {
    std::error_code ec;
    auto t = fs::last_write_time(file_name, ec);
    if (!ec) {
        return ToTimeT(t);
    }
    return 0;
}

bool FileExist(const std::string& file_path) {
    std::error_code ec;
    return fs::exists(file_path, ec);
}

size_t GetFileSize(const std::string& file_name) {
    std::error_code ec;
    auto size = fs::file_size(file_name, ec);
    if (!ec) {
        return size;
    }
    return 0;
}

std::string ReadFile(const std::string& file_name, size_t max_size) {
    if (file_name.empty()) {
        return "";
    }
    std::string buf;
    FilePtr file(fopen(file_name.c_str(), "rb"), &fclose);
    if (file) {
        fseek(file.get(), 0, SEEK_END);
        long tell_result = ftell(file.get());
        if (tell_result > 0) {
            auto size       = static_cast<size_t>(tell_result);
            size_t readsize = max_size < size ? max_size : size;
            buf.resize(readsize);
            fseek(file.get(), 0, SEEK_SET);
            size_t bytes_read = fread(buf.data(), 1, readsize, file.get());
            buf.resize(bytes_read);
        }
    }
    return buf;
}

std::vector<uint8_t> ReadFileBin(const std::string& file_name, size_t max_size) {
    if (file_name.empty()) {
        return {};
    }
    std::vector<uint8_t> buf;
    FilePtr file(fopen(file_name.c_str(), "rb"), &fclose);
    if (file) {
        fseek(file.get(), 0, SEEK_END);
        long tell_result = ftell(file.get());
        if (tell_result > 0) {
            auto size = static_cast<size_t>(tell_result);
            buf.resize(max_size < size ? max_size : size);
            fseek(file.get(), 0, SEEK_SET);
            size_t bytes_read = fread(buf.data(), 1, buf.size(), file.get());
            buf.resize(bytes_read);
        }
    }
    return buf;
}

bool WriteFile(const std::string& file_name, const std::string& data) {
    FilePtr file(fopen(file_name.c_str(), "wb"), &fclose);
    if (file) {
        return fwrite(data.c_str(), 1, data.size(), file.get()) == data.size();
    }
    return false;
}

bool WriteFileAppend(const std::string& file_name, const std::string& data) {
    FilePtr file(fopen(file_name.c_str(), "a+"), &fclose);
    if (file) {
        return fwrite(data.c_str(), 1, data.size(), file.get()) == data.size();
    }
    return false;
}

bool WriteFileAppend(const std::string& file_name, const std::uint8_t* data, int size) {
    FilePtr file(fopen(file_name.c_str(), "a+"), &fclose);
    if (file) {
        return fwrite(data, 1, static_cast<size_t>(size), file.get()) == static_cast<size_t>(size);
    }
    return false;
}

bool WriteFile(const std::string& file_name, const std::uint8_t* data, int size) {
    if ((size <= 0) || (!data)) {
        return false;
    }
    FilePtr file(fopen(file_name.c_str(), "wb"), &fclose);
    if (file) {
        return fwrite(data, 1, static_cast<size_t>(size), file.get()) == static_cast<size_t>(size);
    }
    return false;
}

std::vector<std::string> GetSubDir(const std::string& base_dir, bool add_base) {
    std::vector<std::string> sub_dirs;
    std::error_code ec;
    if (!fs::exists(base_dir, ec) || !fs::is_directory(base_dir, ec)) {
        return sub_dirs;
    }

    for (const auto& entry : fs::directory_iterator(base_dir, ec)) {
        if (entry.is_directory(ec)) {
            if (add_base) {
                sub_dirs.push_back(entry.path().string());
            } else {
                sub_dirs.push_back(entry.path().filename().string());
            }
        }
    }
    return sub_dirs;
}

std::string BaseSimplify(const std::string& path) {
    std::string result;
    size_t idx = 0;
    while (idx < path.size()) {
        if ('\\' == path[idx] || '/' == path[idx]) {
            result.append("/", 1);
            while ((idx + 1 < path.size()) && ('\\' == path[idx + 1] || '/' == path[idx + 1])) {
                ++idx;
            }
        } else {
            result += path[idx];
        }
        ++idx;
    }

    if (result.size() > 1 && '/' == result.back()) {
        result.pop_back();
    }

    return result;
}

// CheckExist removed — identical to FileExist. Use FileExist instead.

bool CreateDir(const std::string& dir, bool clear) {
    std::string simplified_path = BaseSimplify(dir);
    if (simplified_path.empty())
        return false;

    std::error_code ec;
    fs::create_directories(simplified_path, ec);
    if (ec) {
        return false;
    }

    if (clear) {
        for (const auto& entry : fs::directory_iterator(simplified_path, ec)) {
            if (entry.is_regular_file(ec)) {
                auto lwt = fs::last_write_time(entry, ec);
                if (!ec) {
                    auto time_diff = fs::file_time_type::clock::now() - lwt;
                    if (std::chrono::duration_cast<std::chrono::seconds>(time_diff).count() >= 3600) {
                        fs::remove(entry.path(), ec);
                    }
                }
            }
        }
    }
    return true;
}

std::vector<std::string> GetAllFileName(const std::string& dir, const std::string& filter,
                                        FileAttr file_type) {
    std::vector<std::string> file_names;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return file_names;
    }

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        bool is_match = false;
        if (file_type == FileAttr::FileAttrDir && entry.is_directory(ec)) {
            is_match = true;
        } else if (file_type == FileAttr::FileAttrFile && entry.is_regular_file(ec)) {
            is_match = true;
        }

        if (is_match) {
            std::string filename = entry.path().filename().string();
            if (filter.empty() || filename.find(filter) != std::string::npos) {
                file_names.push_back(std::move(filename));
            }
        }
    }
    return file_names;
}

std::string GetParentPath(const std::string& file_name) {
    fs::path file(file_name);
    return file.parent_path().string();
}

std::string GetFileName(const std::string& file_name) {
    fs::path file(file_name);
    return file.filename().string();
}

std::string RemoveExtension(const std::string& file_path) {
    size_t last_slash_pos = file_path.find_last_of('/');
    if (last_slash_pos == std::string::npos) {
        return file_path;
    }

    std::string file_name_part = file_path.substr(last_slash_pos + 1);
    size_t last_dot_pos        = file_name_part.find_last_of('.');

    if (last_dot_pos == std::string::npos) {
        return file_path.substr(0, last_slash_pos + 1);
    }

    return file_path.substr(0, last_slash_pos + 1) + file_name_part.substr(0, last_dot_pos);
}

namespace {

    bool CopyAndDelete(const fs::path& source, const fs::path& target) {
        std::error_code ec;
        if (!fs::exists(target.parent_path(), ec)) {
            fs::create_directories(target.parent_path(), ec);
        }

        if (!fs::exists(source, ec)) {
            LOG_WARN("Source file does not exist: {}", source.string());
            return false;
        }

        std::ifstream src(source, std::ios::binary);
        if (!src) {
            LOG_WARN("Cannot open source file: {}", source.string());
            return false;
        }
        std::ofstream dst(target, std::ios::binary);
        if (!dst) {
            LOG_WARN("Cannot open target file: {}", target.string());
            return false;
        }
        dst << src.rdbuf();
        if (!dst.good()) {
            LOG_ERRO("Failed to write target file: {}", target.string());
            return false;
        }
        dst.close();

        fs::remove(source, ec);
        if (ec) {
            LOG_WARN("Failed to remove source after copy: {}, {}", source.string(), ec.message());
            return false;
        }
        LOG_INFO("File copied and deleted: {} -> {}", source.string(), target.string());
        return true;
    }

    bool IsRegularFile(const fs::path& path) {
        std::error_code ec;
        return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

}  // anonymous namespace

bool FileMove(const std::string& file_name, const std::string& path) {
    std::error_code ec;
    fs::path source_path(file_name);
    fs::path dest_dir(path);

    std::string file_name_without_path = source_path.filename().string();
    fs::path new_file                  = fs::absolute(dest_dir, ec) / file_name_without_path;

    if (!fs::is_directory(source_path, ec)) {
        if (!CopyAndDelete(source_path, new_file)) {
            return false;
        }
        LOG_INFO("Single file moved: {} -> {}", file_name, new_file.string());
        return true;
    }

    if (!fs::exists(new_file, ec)) {
        fs::create_directories(new_file, ec);
    }

    for (const auto& entry : fs::directory_iterator(source_path, ec)) {
        if (IsRegularFile(entry.path())) {
            fs::path source_file      = entry.path();
            fs::path destination_file = new_file / source_file.filename();
            if (!CopyAndDelete(source_file, destination_file)) {
                return false;
            }
        }
    }
    LOG_INFO("All files moved to {}", new_file.string());
    return true;
}

bool FileMoveWithRename(const std::string& file_name, const std::string& new_file) {
    std::error_code ec;
    fs::rename(file_name, new_file, ec);
    return !ec;
}

bool RemovePath(const std::string& path) {
    std::error_code ec;
    fs::path file_path(path);
    LOG_INFO("Removing path: {}", path);
    fs::remove_all(file_path, ec);
    if (ec) {
        LOG_ERRO("Failed to remove {}: {}", path, ec.message());
        return false;
    }
    return true;
}

bool RemoveFile(const std::string& file_name) {
    std::error_code ec;
    fs::remove(file_name, ec);
    return !ec;
}

std::string FindPrefixedJsonFile(const std::string& dir, const std::string& prefix) {
    const std::string search_prefix = prefix + "_";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return "";
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        const std::string filename = entry.path().filename().string();
        if (filename.size() > search_prefix.size() + 5 &&
            filename.compare(0, search_prefix.size(), search_prefix) == 0 &&
            filename.compare(filename.size() - 5, 5, ".json") == 0) {
            return entry.path().string();
        }
    }
    return "";
}

}  // namespace cosmo::util
