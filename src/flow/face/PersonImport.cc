// PersonImport — Person Import implementation.

#include "flow/face/PersonImport.h"

#include <iconv.h>

#include <filesystem>
#include <fstream>

#include "flow/face/FacePic.h"
#include "flow/face/FaceRegLib.h"
#include "flow/face/Person.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRepo.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/DurationLogger.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"
#include "util/Transcode.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace fs = std::filesystem;

static constexpr const char* kTag           = "PersonImport";
static constexpr float kImportQuality       = 80.0f;
static constexpr int kDbBatchCommitInterval = 1000;
static const std::vector<std::string> kValidImageExtensions{".png", ".jpg", ".jpeg"};

PersonImport::PersonImport() : error_file_(fs::path(cosmo::path::GetWebLocalPath()) / "importerror.csv") {}

PersonImport::~PersonImport() {
    // Wait for any running async import to finish before destruction
    if (future_.valid()) {
        try {
            future_.wait();
        } catch (...) {
            // Swallow exceptions during destruction
        }
    }
}

bool PersonImport::ImportFile(const std::string& file_name, const std::string& face_lib_id) {
    auto file_real_name = util::GetFileName(file_name);
    try {
        if (future_.valid()) {
            if (future_.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
                return false;
            }
            future_.get();
        }
    } catch (const std::exception& e) {
        std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_);
        LOG_ERRO("{} {}", kTag, e.what());
    }

    fs::path file_path{file_name};
    auto face_lib = service::ServiceRegistry::Instance().Get<service::IFaceLibRepo>().GetFaceLib(face_lib_id);
    if (!face_lib) {
        LOG_ERRO("{} Failed to query face library, {}", kTag, face_lib_id);
        remove(file_path);
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Face library ID query failed");
    }

    if (!exists(file_path) || file_path.extension() != ".zip") {
        LOG_ERRO("{} Import file error, {}", kTag, file_path);
        remove(file_path);
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "File validation error");
    }

    auto id = util::GenerateUUID();

    auto temp_path = fs::path(cosmo::path::GetTemporaryDirPath());
    std::filesystem::create_directories(temp_path);
    std::error_code cp_ec;
    std::filesystem::copy(file_name, temp_path / file_real_name,
                          std::filesystem::copy_options::overwrite_existing, cp_ec);
    if (cp_ec) {
        LOG_ERRO("{} Copy failed: {} -> {}: {}", kTag, file_name, temp_path.string(), cp_ec.message());
    }
    temp_path = temp_path / file_real_name;
    LOG_INFO("{} File move and backup completed {} {}", kTag, temp_path, query_id_);

    completed_.store(false, std::memory_order_release);
    future_ = async(std::launch::async, [this, temp_path, face_lib, id]() {
        LOG_INFO("{} Ready to import face", kTag);
        DoImportFile(temp_path, face_lib, id);
        completed_.store(true, std::memory_order_release);
        LOG_INFO("{} Import face completed", kTag);
    });
    return true;
}

void PersonImport::UnzipArchive(const std::string& file_name, const std::string& work_dir) {
    std::filesystem::create_directories(work_dir);
    // Fix occasional decompression garbled text issue on 1800, reference:
    // https://blog.csdn.net/guo_qiangqiang/article/details/107163832
    std::string cmd_str{"unzip " + util::ShellEscape(file_name) + " -d " + util::ShellEscape(work_dir)};
    std::string unzip_out;
    int unzip_ret = util::Exec(cmd_str, unzip_out);
    if (unzip_ret != 0) {
        LOG_WARN("{} unzip exit code {}: {}", kTag, unzip_ret, unzip_out);
    }
    remove(fs::path{file_name});
    LOG_INFO("{} Unzip command {}", kTag, cmd_str);
}

void PersonImport::ProcessImages(const std::string& work_dir, FaceLibPtr face_lib, std::ofstream& ofile) {
    util::Transcode transcode("UTF-8", "GB18030");

    // Count total files
    for (auto& dir_entry : fs::recursive_directory_iterator(work_dir)) {
        if (fs::is_directory(dir_entry.status())) {
            continue;
        }
        std::lock_guard<std::shared_mutex> lock(mtx_);
        ++total_;
    }

    fs::path unzip_path{"/usr/bin/unzip"};
    // Photos decompressed by busybox have original encoding, normal unzip has UTF-8, no conversion needed
    bool need_transcode = fs::is_symlink(unzip_path) && fs::read_symlink(unzip_path).filename() == "busybox";

    size_t face_count = face_lib->GetFaceCount();
    size_t max_count  = face_lib->GetFaceMaxCount();
    util::DurationLogger logger_total("import face recognise");
    auto& dao_svc     = service::ServiceRegistry::Instance().Get<service::IPersonDaoService>();
    auto& codec_svc   = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>();
    auto& feature_svc = service::ServiceRegistry::Instance().Get<service::IFaceFeature>();
    dao_svc.Begin();
    LOG_INFO("{} Unzip completed, pic count is {} {}", kTag, total_, work_dir);

    for (auto& dir_entry : fs::recursive_directory_iterator(work_dir)) {
        if (fs::is_directory(dir_entry.status())) {
            continue;
        }
        fs::path name;
        if (need_transcode) {
            name = transcode.Convert(dir_entry.path().filename().string());
        } else {
            name = dir_entry.path().filename();
        }
        std::string raw_path = name.string();
        raw_path.erase(std::remove(raw_path.begin(), raw_path.end(), '"'), raw_path.end());

        if (face_count >= max_count) {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            ++failed_;
            ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                  << "Face library capacity reached limit"
                  << "\t\",\"";
            ofile << "\t\"\n";
            continue;
        }
        auto it = find_if(kValidImageExtensions.begin(), kValidImageExtensions.end(),
                          [&dir_entry](const auto& ext) {
                              return util::ToLower(dir_entry.path().extension().string()) == ext;
                          });
        if (it == kValidImageExtensions.end()) {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            ++failed_;
            ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                  << "Invalid image format"
                  << "\t\",\"";
            ofile << "\t\"\n";
            continue;
        }

        auto person = std::make_shared<Person>(std::string{});
        person->SetName(name.stem());
        person->SetCreateTime(util::GetMilliseconds());
        person->SetUpdateTime(person->GetCreateTime());
        VideoFramePtr image;
        VideoFramePtr cut_image;
        std::vector<FacePicPtr> face_pics;
        auto file_data = util::ReadFileBin(dir_entry.path().string());
        if (!file_data.empty()) {
            image = codec_svc.DecodeJpeg(file_data);
        }
        if (!image) {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            ++failed_;
            ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                  << "Image too small or decode failed"
                  << "\t\",\"";
            ofile << "\t\"\n";
            continue;
        }
        try {
            auto id = util::GenerateUUID();
            AiFeature feature;
            auto temp = feature_svc.ExtractFaceFeature(image, kImportQuality, feature, cut_image);
            if (util::ErrorEnum::Success != temp) {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                      << "get face feature failed"
                      << "\t\",\"";
                ofile << "\t\"\n";
                continue;
            }
            auto orig_jpeg         = codec_svc.EncodeJpeg(cut_image);
            std::string photo_path = (fs::path(cosmo::path::GetFaceLibPhotoDir()) / id).concat(".jpg");
            if (util::WriteFile(photo_path, reinterpret_cast<const std::uint8_t*>(orig_jpeg.data()),
                                static_cast<int>(orig_jpeg.size()))) {
                face_pics.push_back(
                    std::make_shared<FacePic>(id, cosmo::path::GetFaceLibPhotoDir(), feature));
            } else {
                LOG_ERRO("{} write file error", kTag);
            }
        } catch (const util::ErrorMessage& e) {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            ++failed_;
            ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\"" << e.what() << "\t\",\"";
            ofile << "\t\"\n";
            continue;
        }
        if (!face_pics.empty()) {
            service::ServiceRegistry::Instance().Get<service::IPersonRepo>().AddPerson(face_lib, person);
            for (auto& pic : face_pics) {
                person->AddPicture(pic);
            }

            db::FaceRegRecordUnit face_record_unit;
            face_record_unit.face_update_time = person->GetUpdateTime();
            face_record_unit.face_create_time = person->GetCreateTime();
            face_record_unit.face_name        = person->GetName();
            face_record_unit.serial_name      = person->GetSerialNumber();
            face_record_unit.id               = person->GetId();
            for (size_t i = 0; i < person->GetFaceLibId().size(); i++) {
                face_record_unit.face_lib_id.push_back(person->GetFaceLibId()[i]);
            }
            for (size_t i = 0; i < person->GetPictures().size(); i++) {
                db::FacePicInfo temp;
                temp.id      = person->GetPictures()[i]->GetId();
                temp.feature = person->GetPictures()[i]->GetFeature().feature;
                face_record_unit.face_pic_infos.push_back(temp);
            }
            dao_svc.AddPerson(face_record_unit);

            ++face_count;
            int success = 0;
            {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                success = ++success_;
            }
            if (success % kDbBatchCommitInterval == 0) {
                dao_svc.Commit();
                dao_svc.Begin();
            }
        } else {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            ++failed_;
            ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\"";
            ofile << "\t\"\n";
        }
    }
    dao_svc.Commit();
    // Release VRAM of face feature extraction model after batch import
    feature_svc.ReleaseFaceModels();
}

void PersonImport::DoImportFile(const std::string& file_name, FaceLibPtr face_lib, const std::string& uuid) {
    Clean();
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        query_id_ = uuid;
    }
    LOG_INFO("{} import file {} {}", kTag, file_name.c_str(), query_id_);

    auto work_dir = (fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_).string();

    UnzipArchive(file_name, work_dir);

    std::ofstream ofile(error_file_);
    unsigned char szUTF_8BOM[] = {0xEF, 0xBB, 0xBF, 0};
    ofile << szUTF_8BOM;
    ofile << "\"No.\",\"Photo Name\",\"Failure Reason\"\n";

    ProcessImages(work_dir, face_lib, ofile);

    std::filesystem::remove_all(work_dir);
}

std::string PersonImport::GetQueryId() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return query_id_;
}

int PersonImport::GetTotalCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return total_;
}

std::pair<int, int> PersonImport::GetStatus() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return {success_, failed_};
}

bool PersonImport::Complete() const {
    return completed_.load(std::memory_order_acquire);
}

void PersonImport::Clean() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    total_   = 0;
    success_ = 0;
    failed_  = 0;
    if (!query_id_.empty()) {
        std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_);
    }
    query_id_.clear();
}

std::string PersonImport::GetFailedUrl() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return fs::path(cosmo::path::GetWebAcessPath()) / "importerror.csv";
}

}  // namespace cosmo
