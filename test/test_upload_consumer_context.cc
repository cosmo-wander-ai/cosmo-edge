#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageAlgorithmHandler.h"
#include "api/MessageCameraHandler.h"
#include "api/MessageImportFileHandler.h"
#include "api/MessageModelHandler.h"
#include "api/MessageSystemHandler.h"
#include "api/StagedImageInput.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockAudioService.h"
#include "mock/MockCameraService.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockModelService.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTaskService.h"
#include "mock/MockTimeService.h"
#include "service/path/IUploadStagingService.h"
#include "service/path/impl/UploadStagingServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/CipherUtil.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace {

    namespace fs = std::filesystem;
    using trompeloeil::_;

    struct ModelConsumerMocks {
        test::MockModelService modelSvc;
    };

    struct CameraConsumerMocks {
        test::MockCameraService cameraSvc;
        test::MockTaskService taskSvc;
    };

    struct AlgorithmConsumerMocks {
        test::MockAlgorithmService algSvc;
        test::MockCameraService cameraSvc;
        test::MockTaskService taskSvc;
    };

    struct SystemConsumerMocks {
        test::MockConfigReadService configReadSvc;
        test::MockConfigWriteService configWriteSvc;
        test::MockConfigNetworkService configNetSvc;
        test::MockDeviceInfoService deviceInfoSvc;
        test::MockSystemOperationService systemOpSvc;
        test::MockTimeService timeSvc;
    };

    class TempDirectory {
    public:
        TempDirectory() {
            path_ = fs::path("/tmp") / ("cosmo-upload-consumer-" + util::GenerateUUID());
            fs::create_directories(path_);
        }

        ~TempDirectory() {
            std::error_code error;
            fs::remove_all(path_, error);
        }

        [[nodiscard]] const fs::path& Path() const {
            return path_;
        }

    private:
        fs::path path_;
    };

    service::UploadStagingConfig MakeConfig(const fs::path& root) {
        service::UploadStagingConfig config;
        config.root_path                  = root.string();
        config.max_total_size             = 2ULL * 1024 * 1024;
        config.max_chunk_size             = 2ULL * 1024 * 1024;
        config.max_chunks                 = 4;
        config.max_sessions_per_principal = 8;
        config.max_sessions               = 8;
        config.max_reserved_bytes         = 16ULL * 1024 * 1024;
        config.reserve_free_bytes         = 0;
        config.reserve_free_percent       = 0;
        return config;
    }

    struct StagedUpload {
        std::string upload_id;
        std::string legacy_reference;
        std::string server_path;
    };

    StagedUpload StageFile(service::UploadStagingServiceImpl& staging, const fs::path& temp,
                           const std::string& principal, service::UploadPurpose purpose,
                           const std::string& name, const std::string& content) {
        const auto chunk = temp / ("chunk-" + util::GenerateUUID());
        std::ofstream output(chunk, std::ios::binary | std::ios::trunc);
        REQUIRE(output.is_open());
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        REQUIRE(output.good());

        service::UploadBeginRequest request;
        request.principal     = principal;
        request.purpose       = purpose;
        request.original_name = name;
        request.total_size    = content.size();
        request.total_chunks  = 1;
        service::UploadSessionInfo info;
        REQUIRE(staging.Begin(request, info) == util::ErrorEnum::Success);
        REQUIRE(staging.AppendChunk(principal, info.upload_id, 0, chunk.string(), info) ==
                util::ErrorEnum::Success);
        REQUIRE(staging.Complete(principal, info.upload_id, info) == util::ErrorEnum::Success);
        StagedUpload upload;
        upload.upload_id   = info.upload_id;
        upload.server_path = (temp / "sessions" / info.upload_id / name).string();
        REQUIRE(staging.GetLegacyPath(principal, info.upload_id, purpose, upload.legacy_reference) ==
                util::ErrorEnum::Success);
        return upload;
    }

    RequestDispatchContext HttpContext(const std::string& principal) {
        RequestDispatchContext context;
        context.credential = principal;
        context.principal  = principal;
        context.transport  = RequestTransport::kHttp;
        return context;
    }

    RequestDispatchContext MqttContext(const std::string& principal) {
        auto context      = HttpContext(principal);
        context.transport = RequestTransport::kMqtt;
        return context;
    }

    RequestDispatchContext MultipartContext(const std::string& principal, const fs::path& path,
                                            const std::string& name, std::uint64_t size) {
        auto context                = HttpContext(principal);
        context.multipart_file_path = path.string();
        context.multipart_file_name = name;
        context.multipart_file_size = size;
        return context;
    }

}  // namespace

TEST_CASE("Upload consumers enforce owner purpose and one-shot model paths", "[upload-staging][api]") {
    ModelConsumerMocks mocks;
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);
    MessageModelHandler handler(mocks.modelSvc);

    SECTION("a matching owner can consume an archive only once") {
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelArchive,
                                      "model.zip", "archive");
        REQUIRE_CALL(mocks.modelSvc, ModelAdd(upload.server_path)).RETURN(util::ErrorEnum::Success);

        Model::MsgUploadRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));

        Model::MsgUploadRecv replay;
        replay.uploadId = upload.upload_id;
        error.clear();
        (void)handler.Handle(std::move(replay), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::NoSuchId);
    }

    SECTION("model import consumes a formal archive ID") {
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelArchive,
                                      "import.zip", "archive");
        REQUIRE_CALL(mocks.modelSvc, ImportModel(upload.server_path)).RETURN(util::ErrorEnum::Success);

        Model::MsgImportModelRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("R1 accepts only the registered opaque compatibility reference") {
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelArchive,
                                      "legacy.zip", "archive");
        REQUIRE_CALL(mocks.modelSvc, ModelAdd(upload.server_path)).RETURN(util::ErrorEnum::Success);

        Model::MsgUploadRecv request;
        request.filePath = upload.legacy_reference;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("a different principal cannot consume a formal ID") {
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelArchive,
                                      "private.zip", "archive");
        Model::MsgUploadRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("other"), error);
        CHECK(error == util::ErrorEnum::AuthFailed);
        CHECK(fs::exists(upload.server_path));
        CHECK(staging.Cancel("owner", upload.upload_id) == util::ErrorEnum::Success);
    }

    SECTION("all atomic model components are claimed together by formal IDs") {
        const auto model = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelComponent,
                                     "model.bmodel", "model");
        const auto vocab = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelComponent,
                                     "vocab.txt", "vocab");
        const auto tokenizer =
            StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelComponent,
                      "tokenizer.json", "tokenizer");
        const auto character_table =
            StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelComponent,
                      "characters.txt", "characters");
        REQUIRE_CALL(mocks.modelSvc, AddAtomicModel(_, _, _, _, _, vocab.server_path, tokenizer.server_path,
                                                    character_table.server_path, _, _))
            .WITH(_5.size() == 1 && _5.front().filePath == model.server_path)
            .RETURN(util::ErrorEnum::Success);

        Model::MsgAddRecv request;
        Model::BmodelFileInfo file;
        file.role     = "main";
        file.uploadId = model.upload_id;
        request.bmodelFiles.push_back(file);
        request.vocabUploadId          = vocab.upload_id;
        request.tokenizerUploadId      = tokenizer.upload_id;
        request.characterTableUploadId = character_table.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(model.server_path));
        CHECK_FALSE(fs::exists(vocab.server_path));
        CHECK_FALSE(fs::exists(tokenizer.server_path));
        CHECK_FALSE(fs::exists(character_table.server_path));
    }

    SECTION("batch component consumption is atomic across owners") {
        const auto owner_upload = StageFile(staging, temp.Path(), "owner",
                                            service::UploadPurpose::kModelComponent, "owner.bmodel", "a");
        const auto other_upload = StageFile(staging, temp.Path(), "other",
                                            service::UploadPurpose::kModelComponent, "other.bmodel", "b");

        Model::MsgAddRecv request;
        Model::BmodelFileInfo encoder;
        encoder.role     = "encoder";
        encoder.uploadId = owner_upload.upload_id;
        Model::BmodelFileInfo decoder;
        decoder.role        = "decoder";
        decoder.uploadId    = other_upload.upload_id;
        request.bmodelFiles = {encoder, decoder};
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::AuthFailed);

        std::string still_registered;
        CHECK(staging.GetLegacyPath("owner", owner_upload.upload_id, service::UploadPurpose::kModelComponent,
                                    still_registered) == util::ErrorEnum::Success);
        CHECK(staging.GetLegacyPath("other", other_upload.upload_id, service::UploadPurpose::kModelComponent,
                                    still_registered) == util::ErrorEnum::Success);
        CHECK(staging.Cancel("owner", owner_upload.upload_id) == util::ErrorEnum::Success);
        CHECK(staging.Cancel("other", other_upload.upload_id) == util::ErrorEnum::Success);
    }
}

TEST_CASE("Upload consumers bind purpose before invoking business services", "[upload-staging][api]") {
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);

    SECTION("algorithm upload") {
        AlgorithmConsumerMocks mocks;
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kAlgorithm,
                                      "algorithm.zip", "archive");
        REQUIRE_CALL(mocks.algSvc, Add(upload.server_path)).RETURN(util::ErrorEnum::Success);
        MessageAlgorithmHandler handler(mocks.algSvc, mocks.algSvc, mocks.algSvc, mocks.cameraSvc,
                                        mocks.taskSvc);
        Algorithm::MsgUploadRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("face import") {
        test::MockFaceLibService face_import_service;
        test::ScopedServiceOverride<service::IFaceImport> face_import(face_import_service);
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kFaceImport,
                                      "faces.zip", "archive");
        REQUIRE_CALL(face_import_service, ImportFile(upload.server_path, "library"));
        MessageImportFileHandler handler;
        service::MsgImportFileRecv request;
        request.importType = 2;
        request.uploadId   = upload.upload_id;
        request.faceLibId  = "library";
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("audio import") {
        test::MockAudioService audio_service;
        test::ScopedServiceOverride<service::IAudioService> audio(audio_service);
        const auto upload =
            StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kAudio, "alert.wav", "audio");
        REQUIRE_CALL(audio_service, AudioFileCount()).RETURN(0U);
        REQUIRE_CALL(audio_service, AudioFileMaxCount()).RETURN(10U);
        REQUIRE_CALL(audio_service, AddAudioFile(upload.server_path)).RETURN(true);
        MessageImportFileHandler handler;
        service::MsgImportFileRecv request;
        request.importType = 5;
        request.uploadId   = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("system upgrade") {
        SystemConsumerMocks mocks;
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kUpgrade,
                                      "cosmo-V1.0.0-00000000000000000000000000000000.tar.gz", "archive");
        REQUIRE_CALL(mocks.systemOpSvc, Upgrade(upload.server_path)).RETURN(util::ErrorEnum::Success);
        MessageSystemHandler handler(mocks.configReadSvc, mocks.configWriteSvc, mocks.configNetSvc,
                                     mocks.deviceInfoSvc, mocks.systemOpSvc, mocks.timeSvc);
        System::MsgUpgradeRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("local video uses the staged file size") {
        CameraConsumerMocks mocks;
        const std::string content(1024 * 1024, 'v');
        const auto upload =
            StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kVideo, "video.mp4", content);
        REQUIRE_CALL(mocks.cameraSvc, Add(_, _)).RETURN(util::ErrorEnum::Success);
        MessageCameraHandler handler(mocks.cameraSvc, mocks.cameraSvc, mocks.cameraSvc, mocks.taskSvc);
        camera::MsgAddVideoRecv request;
        request.uploadId      = upload.upload_id;
        request.contentLength = "0";
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK_FALSE(fs::exists(upload.server_path));
    }

    SECTION("purpose mismatch fails before the business service") {
        AlgorithmConsumerMocks mocks;
        const auto upload = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kModelArchive,
                                      "not-an-algorithm.zip", "archive");
        MessageAlgorithmHandler handler(mocks.algSvc, mocks.algSvc, mocks.algSvc, mocks.cameraSvc,
                                        mocks.taskSvc);
        Algorithm::MsgUploadRecv request;
        request.uploadId = upload.upload_id;
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::InvalidParam);
        CHECK(fs::exists(upload.server_path));
        CHECK(staging.Cancel("owner", upload.upload_id) == util::ErrorEnum::Success);
    }
}

TEST_CASE("Staged image input is authenticated purpose-bound and one-shot", "[upload-staging][api][image]") {
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);

    const auto encoded_image_bytes = util::DecBase64Vec(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAusB9Y9Zl1sAAAAASUVORK5CYII=");
    REQUIRE_FALSE(encoded_image_bytes.empty());
    const std::string encoded_image(reinterpret_cast<const char*>(encoded_image_bytes.data()),
                                    encoded_image_bytes.size());
    const auto image = StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kImage,
                                 "high-resolution.png", encoded_image);

    std::vector<std::vector<std::uint8_t>> images;
    CHECK(detail::ConsumeStagedImages(HttpContext("other"), {image.upload_id}, images) ==
          util::ErrorEnum::AuthFailed);
    CHECK(images.empty());

    CHECK(detail::ConsumeStagedImages(MqttContext("owner"), {image.upload_id}, images) ==
          util::ErrorEnum::InvalidParam);
    CHECK(images.empty());

    REQUIRE(detail::ConsumeStagedImages(HttpContext("owner"), {image.upload_id}, images) ==
            util::ErrorEnum::Success);
    REQUIRE(images.size() == 1);
    CHECK(std::string(images.front().begin(), images.front().end()) == encoded_image);

    CHECK(detail::ConsumeStagedImages(HttpContext("owner"), {image.upload_id}, images) ==
          util::ErrorEnum::NoSuchId);
    CHECK(images.empty());
}

TEST_CASE("Staged image batches are claimed atomically", "[upload-staging][api][image]") {
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);

    const auto first =
        StageFile(staging, temp.Path(), "owner", service::UploadPurpose::kImage, "first.jpg", "first");
    const auto second =
        StageFile(staging, temp.Path(), "other", service::UploadPurpose::kImage, "second.jpg", "second");

    std::vector<std::vector<std::uint8_t>> images;
    CHECK(detail::ConsumeStagedImages(HttpContext("owner"), {first.upload_id, second.upload_id}, images) ==
          util::ErrorEnum::AuthFailed);
    CHECK(images.empty());

    std::string reference;
    CHECK(staging.GetLegacyPath("owner", first.upload_id, service::UploadPurpose::kImage, reference) ==
          util::ErrorEnum::Success);
    CHECK(staging.GetLegacyPath("other", second.upload_id, service::UploadPurpose::kImage, reference) ==
          util::ErrorEnum::Success);
    CHECK(staging.Cancel("owner", first.upload_id) == util::ErrorEnum::Success);
    CHECK(staging.Cancel("other", second.upload_id) == util::ErrorEnum::Success);
}

TEST_CASE("File-consuming context handlers reject MQTT path requests", "[upload-staging][api][security]") {
    const auto context = MqttContext("owner");

    SECTION("algorithm") {
        AlgorithmConsumerMocks mocks;
        MessageAlgorithmHandler handler(mocks.algSvc, mocks.algSvc, mocks.algSvc, mocks.cameraSvc,
                                        mocks.taskSvc);
        Algorithm::MsgUploadRecv request;
        request.filePath = "/tmp/algorithm.zip";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("model archive") {
        ModelConsumerMocks mocks;
        MessageModelHandler handler(mocks.modelSvc);
        Model::MsgUploadRecv request;
        request.filePath = "/tmp/model.zip";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("model import") {
        ModelConsumerMocks mocks;
        MessageModelHandler handler(mocks.modelSvc);
        Model::MsgImportModelRecv request;
        request.filePath = "/tmp/model.zip";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("model components") {
        ModelConsumerMocks mocks;
        MessageModelHandler handler(mocks.modelSvc);
        Model::MsgAddRecv request;
        Model::BmodelFileInfo file;
        file.role     = "main";
        file.filePath = "/tmp/model.bmodel";
        request.bmodelFiles.push_back(file);
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("upload temp") {
        ModelConsumerMocks mocks;
        MessageModelHandler handler(mocks.modelSvc);
        Model::MsgUploadTempRecv request;
        request.filePath      = "/tmp/model.bmodel";
        request.fileName      = "model.bmodel";
        request.contentLength = "16";
        request.totalSize     = "16";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("face import") {
        MessageImportFileHandler handler;
        service::MsgImportFileRecv request;
        request.importType = 2;
        request.filePath   = "/tmp/faces.zip";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("local video") {
        CameraConsumerMocks mocks;
        MessageCameraHandler handler(mocks.cameraSvc, mocks.cameraSvc, mocks.cameraSvc, mocks.taskSvc);
        camera::MsgAddVideoRecv request;
        request.filePath = "/tmp/video.mp4";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("system upgrade") {
        SystemConsumerMocks mocks;
        MessageSystemHandler handler(mocks.configReadSvc, mocks.configWriteSvc, mocks.configNetSvc,
                                     mocks.deviceInfoSvc, mocks.systemOpSvc, mocks.timeSvc);
        System::MsgUpgradeRecv request;
        request.filePath = "/tmp/cosmo.tar.gz";
        std::error_condition error;
        (void)handler.Handle(std::move(request), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }
}

TEST_CASE("Local video accepts the external channel alias and rejects conflicts",
          "[camera][api][compatibility]") {
    SECTION("externalChannelNo maps to channelCode") {
        const auto request =
            nlohmann::json{{"externalChannelNo", "external-7"}}.get<camera::MsgAddVideoRecv>();
        CHECK(request.channelCode == "external-7");
        CHECK_FALSE(request.channelCodeConflict);
    }

    SECTION("conflicting aliases fail before upload consumption") {
        auto request = nlohmann::json{{"channelCode", "channel-a"}, {"externalChannelNo", "channel-b"}}
                           .get<camera::MsgAddVideoRecv>();
        REQUIRE(request.channelCodeConflict);

        CameraConsumerMocks mocks;
        MessageCameraHandler handler(mocks.cameraSvc, mocks.cameraSvc, mocks.cameraSvc, mocks.taskSvc);
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }
}

TEST_CASE("Local video admission has no fixed one-gigabyte ceiling", "[camera][api][resource-policy]") {
    CameraConsumerMocks mocks;
    REQUIRE_CALL(mocks.cameraSvc, Add(trompeloeil::_, trompeloeil::_)).RETURN(util::ErrorEnum::Success);
    MessageCameraHandler handler(mocks.cameraSvc, mocks.cameraSvc, mocks.cameraSvc, mocks.taskSvc);
    camera::MsgAddVideoRecv request;
    request.filePath      = "/managed/video.mp4";
    request.contentLength = std::to_string(2ULL * 1024 * 1024 * 1024);
    std::error_condition error;
    (void)handler.Handle(std::move(request), error);
    CHECK(error == util::ErrorEnum::Success);
}

TEST_CASE("HTTP upload compatibility only claims the current multipart file",
          "[upload-staging][api][compatibility]") {
    AlgorithmConsumerMocks mocks;
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);

    const std::string content = "legacy-direct-upload";
    const auto request_file   = temp.Path() / "request-file.tar.gz";
    std::ofstream(request_file, std::ios::binary | std::ios::trunc) << content;

    MessageAlgorithmHandler handler(mocks.algSvc, mocks.algSvc, mocks.algSvc, mocks.cameraSvc, mocks.taskSvc);

    SECTION("server provenance is adopted into a private one-shot session") {
        REQUIRE_CALL(mocks.algSvc, Add(ANY(const std::string&)))
            .WITH(_1 != request_file.string())
            .RETURN(util::ErrorEnum::Success);
        Algorithm::MsgUploadRecv request;
        request.filePath = request_file.string();
        std::error_condition error;
        (void)handler.Handle(
            std::move(request),
            MultipartContext("owner", request_file, request_file.filename().string(), content.size()), error);
        CHECK(error == util::ErrorEnum::Success);
        CHECK(fs::exists(request_file));
    }

    SECTION("JSON metadata cannot forge multipart provenance") {
        Algorithm::MsgUploadRecv request;
        request.filePath = request_file.string();
        std::error_condition error;
        (void)handler.Handle(std::move(request), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::NoSuchId);
    }

    SECTION("a different candidate path cannot reuse valid provenance") {
        const auto other_file = temp.Path() / "other.tar.gz";
        std::ofstream(other_file, std::ios::binary | std::ios::trunc) << content;
        Algorithm::MsgUploadRecv request;
        request.filePath = other_file.string();
        std::error_condition error;
        (void)handler.Handle(
            std::move(request),
            MultipartContext("owner", request_file, request_file.filename().string(), content.size()), error);
        CHECK(error == util::ErrorEnum::NoSuchId);
    }

    SECTION("a failed provenance adoption cancels its private session") {
        const auto symlink_path = temp.Path() / "request-file-link.tar.gz";
        fs::create_symlink(request_file, symlink_path);
        Algorithm::MsgUploadRecv request;
        request.filePath = symlink_path.string();
        std::error_condition error;
        (void)handler.Handle(
            std::move(request),
            MultipartContext("owner", symlink_path, symlink_path.filename().string(), content.size()), error);
        CHECK(error == util::ErrorEnum::FileOpenFailed);
        CHECK(fs::directory_iterator(temp.Path() / "sessions") == fs::directory_iterator{});
    }

    SECTION("a failed completion cancels its private session") {
        Algorithm::MsgUploadRecv request;
        request.filePath = request_file.string();
        std::error_condition error;
        (void)handler.Handle(
            std::move(request),
            MultipartContext("owner", request_file, request_file.filename().string(), content.size() + 1),
            error);
        CHECK(error == util::ErrorEnum::InvalidParam);
        CHECK(fs::directory_iterator(temp.Path() / "sessions") == fs::directory_iterator{});
    }
}

TEST_CASE("UploadTemp requires exact server multipart provenance", "[upload-staging][api][security]") {
    ModelConsumerMocks mocks;
    TempDirectory temp;
    service::UploadStagingServiceImpl staging(MakeConfig(temp.Path() / "sessions"));
    test::ScopedServiceOverride<service::IUploadStagingService> registration(staging);
    MessageModelHandler handler(mocks.modelSvc);

    const std::string content = "model-bytes";
    const auto request_file   = temp.Path() / "model.bmodel";
    std::ofstream(request_file, std::ios::binary | std::ios::trunc) << content;

    auto make_request = [&]() {
        Model::MsgUploadTempRecv request;
        request.filePath      = request_file.string();
        request.fileName      = request_file.filename().string();
        request.contentLength = std::to_string(content.size());
        request.totalSize     = std::to_string(content.size());
        request.totalChunks   = "1";
        request.chunkIndex    = "0";
        request.chunkSize     = std::to_string(content.size());
        request.purpose       = "model-component";
        return request;
    };

    SECTION("a JSON request cannot make the service read a local path") {
        std::error_condition error;
        (void)handler.Handle(make_request(), HttpContext("owner"), error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("the parser-reported path name and size are all bound") {
        auto context =
            MultipartContext("owner", request_file, request_file.filename().string(), content.size());
        std::error_condition error;
        auto response = handler.Handle(make_request(), context, error);
        REQUIRE(error == util::ErrorEnum::Success);
        REQUIRE(response.resData.complete);
        REQUIRE_FALSE(response.resData.uploadId.empty());
        CHECK(staging.Cancel("owner", response.resData.uploadId) == util::ErrorEnum::Success);

        context.multipart_file_size += 1;
        error.clear();
        (void)handler.Handle(make_request(), context, error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }

    SECTION("a lost first response retries through one client request ID") {
        auto request            = make_request();
        request.clientRequestId = "first-block-response-lost";
        const auto context =
            MultipartContext("owner", request_file, request_file.filename().string(), content.size());
        std::error_condition error;
        const auto first = handler.Handle(Model::MsgUploadTempRecv(request), context, error);
        REQUIRE(error == util::ErrorEnum::Success);
        REQUIRE(first.resData.complete);
        REQUIRE_FALSE(first.resData.uploadId.empty());

        error.clear();
        const auto replay = handler.Handle(std::move(request), context, error);
        REQUIRE(error == util::ErrorEnum::Success);
        CHECK(replay.resData.complete);
        CHECK(replay.resData.uploadId == first.resData.uploadId);
        CHECK(staging.Cancel("owner", first.resData.uploadId) == util::ErrorEnum::Success);
    }

    SECTION("a failed chunk-zero replay does not cancel the existing aliased session") {
        constexpr auto client_request_id = "existing-zero-chunk-session";
        service::UploadBeginRequest begin_request{
            "owner", service::UploadPurpose::kModelComponent, "model.bmodel", content.size(), 1, {}};
        begin_request.client_request_id = client_request_id;
        service::UploadSessionInfo existing;
        REQUIRE(staging.Begin(begin_request, existing) == util::ErrorEnum::Success);
        REQUIRE(existing.newly_created);

        const auto rejected_source = temp.Path() / "replayed-chunk-symlink";
        fs::create_symlink(request_file, rejected_source);
        auto replay_request            = make_request();
        replay_request.filePath        = rejected_source.string();
        replay_request.clientRequestId = client_request_id;
        std::error_condition error;
        (void)handler.Handle(std::move(replay_request),
                             MultipartContext("owner", rejected_source, "model.bmodel", content.size()),
                             error);
        CHECK(error == util::ErrorEnum::FileOpenFailed);

        // The Begin above was a replay, not ownership of a new session. Its
        // append failure must leave the original session available to retry.
        REQUIRE(staging.AppendChunk("owner", client_request_id, 0, request_file.string(), existing) ==
                util::ErrorEnum::Success);
        REQUIRE(staging.Complete("owner", client_request_id, existing) == util::ErrorEnum::Success);
        CHECK(staging.Cancel("owner", client_request_id) == util::ErrorEnum::Success);
    }

    SECTION("an R1 client-generated upload ID aliases all chunks") {
        const std::string first_content  = "model-";
        const std::string second_content = "bytes";
        const auto first_path            = temp.Path() / "legacy-first-part";
        const auto second_path           = temp.Path() / "legacy-second-part";
        std::ofstream(first_path, std::ios::binary | std::ios::trunc) << first_content;
        std::ofstream(second_path, std::ios::binary | std::ios::trunc) << second_content;
        const std::string legacy_upload_id = "1712345678901_a1b2c3";

        auto make_chunk = [&](const fs::path& path, const std::string& chunk_content,
                              std::uint32_t chunk_index) {
            Model::MsgUploadTempRecv request;
            request.filePath      = path.string();
            request.fileName      = "legacy.bmodel";
            request.contentLength = std::to_string(chunk_content.size());
            request.totalSize     = std::to_string(first_content.size() + second_content.size());
            request.totalChunks   = "2";
            request.chunkIndex    = std::to_string(chunk_index);
            request.chunkSize     = std::to_string(chunk_content.size());
            request.uploadId      = legacy_upload_id;
            return request;
        };

        std::error_condition error;
        auto first = handler.Handle(
            make_chunk(first_path, first_content, 0),
            MultipartContext("owner", first_path, "legacy.bmodel", first_content.size()), error);
        REQUIRE(error == util::ErrorEnum::Success);
        CHECK_FALSE(first.resData.complete);
        REQUIRE_FALSE(first.resData.uploadId.empty());
        CHECK(first.resData.uploadId != legacy_upload_id);

        error.clear();
        const auto second = handler.Handle(
            make_chunk(second_path, second_content, 1),
            MultipartContext("owner", second_path, "legacy.bmodel", second_content.size()), error);
        REQUIRE(error == util::ErrorEnum::Success);
        CHECK(second.resData.complete);
        CHECK(second.resData.uploadId == first.resData.uploadId);

        service::StagedFileLease lease;
        CHECK(staging.Consume("owner", legacy_upload_id, service::UploadPurpose::kModelComponent, lease) ==
              util::ErrorEnum::Success);
        CHECK(lease.Size() == first_content.size() + second_content.size());
    }

    SECTION("an empty purpose uses only the controlled extension whitelist") {
        const auto verify_inferred_purpose = [&](const std::string& file_name,
                                                 const std::string& client_request_id,
                                                 service::UploadPurpose expected_purpose) {
            auto request     = make_request();
            request.fileName = file_name;
            request.purpose.clear();
            request.clientRequestId = client_request_id;
            std::error_condition error;
            const auto response =
                handler.Handle(std::move(request),
                               MultipartContext("owner", request_file, file_name, content.size()), error);
            REQUIRE(error == util::ErrorEnum::Success);
            REQUIRE(response.resData.complete);

            service::StagedFileLease lease;
            CHECK(staging.Consume("owner", response.resData.uploadId, expected_purpose, lease) ==
                  util::ErrorEnum::Success);
        };

        verify_inferred_purpose("legacy.MP4", "infer-video", service::UploadPurpose::kVideo);
        verify_inferred_purpose("legacy.onnx", "infer-component", service::UploadPurpose::kModelComponent);
        verify_inferred_purpose("legacy.tar.gz", "infer-archive", service::UploadPurpose::kModelArchive);
    }

    SECTION("an empty purpose rejects an unsupported extension") {
        auto request     = make_request();
        request.fileName = "legacy.bin";
        request.purpose.clear();
        request.clientRequestId = "infer-unsupported";
        std::error_condition error;
        (void)handler.Handle(std::move(request),
                             MultipartContext("owner", request_file, "legacy.bin", content.size()), error);
        CHECK(error == util::ErrorEnum::InvalidParam);
    }
}

}  // namespace cosmo
