#include <filesystem>
#include <fstream>

#include "util/PathUtil.h"

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "mock/MockServiceRegistry.h"
#include "service/model/impl/ModelUploadHelper.h"

using namespace cosmo::service;
using namespace cosmo::test;

TEST_CASE("ModelUploadHelper: UploadTempFile Tests", "[model]") {
    MockServiceRegistry mocks;
    ModelUploadHelper uploadHelper;
    std::string persistentPath;

    std::string testDir = "/tmp/cosmo_test_upload";
    cosmo::path::OverrideRootPathForTest(testDir, testDir);
    auto uploadTmpDir = cosmo::path::GetModelUploadTmpDir();

    SECTION("1.1 单文件上传：验证文件正确拷贝到持久化目录") {
        std::string sourceFilePath = "/tmp/cosmo_test_source.bin";
        std::ofstream out(sourceFilePath, std::ios::binary);
        out << "Single File Content";
        out.close();

        auto res =
            uploadHelper.UploadTempFile(sourceFilePath, "test_file.bin", "19", "", "", "", persistentPath);

        REQUIRE(res == cosmo::util::ErrorEnum::Success);
        REQUIRE(!persistentPath.empty());
        REQUIRE(std::filesystem::exists(persistentPath));

        std::ifstream in(persistentPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "Single File Content");

        std::filesystem::remove(sourceFilePath);
        std::filesystem::remove(persistentPath);
    }

    SECTION("1.2 分块上传：验证按序追加、meta 文件管理") {
        std::string chunk1Path = "/tmp/cosmo_test_chunk1.bin";
        std::string chunk2Path = "/tmp/cosmo_test_chunk2.bin";

        std::ofstream out1(chunk1Path, std::ios::binary);
        out1 << "Chunk 1 Content ";
        out1.close();

        std::ofstream out2(chunk2Path, std::ios::binary);
        out2 << "Chunk 2 Content";
        out2.close();

        std::string uploadId = "test_upload_id";

        // Upload Chunk 0
        auto res1 = uploadHelper.UploadTempFile(chunk1Path, "chunked_file.bin", "16", uploadId, "0", "2",
                                                persistentPath);
        REQUIRE(res1 == cosmo::util::ErrorEnum::Success);

        // Note: The destination path is updated on every chunk upload, but only considered complete when last
        // chunk uploads. Chunk 1 gets deleted
        REQUIRE(!std::filesystem::exists(chunk1Path));

        // Upload Chunk 1
        auto res2 = uploadHelper.UploadTempFile(chunk2Path, "chunked_file.bin", "15", uploadId, "1", "2",
                                                persistentPath);
        REQUIRE(res2 == cosmo::util::ErrorEnum::Success);
        REQUIRE(!std::filesystem::exists(chunk2Path));

        REQUIRE(std::filesystem::exists(persistentPath));

        std::ifstream in(persistentPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "Chunk 1 Content Chunk 2 Content");

        std::filesystem::remove(persistentPath);
    }

    SECTION("1.3 分块乱序：验证 InvalidParam 返回") {
        std::string chunk2Path = "/tmp/cosmo_test_chunk_out_of_order.bin";
        std::ofstream out(chunk2Path, std::ios::binary);
        out << "Chunk 2 Content";
        out.close();

        std::string uploadId = "test_upload_id_out_of_order";

        // Try uploading Chunk 1 before Chunk 0
        auto res = uploadHelper.UploadTempFile(chunk2Path, "out_of_order.bin", "15", uploadId, "1", "2",
                                               persistentPath);
        REQUIRE(res == cosmo::util::ErrorEnum::InvalidParam);

        std::filesystem::remove(chunk2Path);
    }

    SECTION("1.4 空文件路径：验证 InvalidParam 返回") {
        auto res = uploadHelper.UploadTempFile("", "empty_path.bin", "0", "", "", "", persistentPath);
        REQUIRE(res == cosmo::util::ErrorEnum::InvalidParam);
    }

    std::filesystem::remove_all(testDir);
}
