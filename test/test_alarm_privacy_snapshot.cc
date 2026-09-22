#include "catch_amalgamated.hpp"
#include "flow/common/AlarmPrivacySnapshot.h"
#include "flow/common/AlgDataUnit.h"

namespace {

// Snapshot tests exercise metadata only and must not require a device/memory-pool context.
VideoFramePtr MetadataFrame(int64_t index = 42, int64_t timestamp = 1000) {
    auto frame = std::make_shared<cosmo::media::VideoFrame>(0, 0);
    frame->SetWidth(640);
    frame->SetHeight(480);
    frame->SetFrameIndex(index);
    frame->SetStreamIndex(7);
    frame->SetTimestamp(timestamp);
    return frame;
}

cosmo::DataDetTrackClassify Detection(const VideoFramePtr& frame, int people = 0) {
    cosmo::DataDetTrackClassify result;
    result.streamIndex = frame->GetStreamIndex();
    result.frameIndex  = frame->GetFrameIndex();
    result.timestamp   = frame->GetTimestamp();
    result.picWidth    = frame->GetWidth();
    result.picHeight   = frame->GetHeight();
    for (int i = 0; i < people; ++i) {
        cosmo::AiDetectRstEl target;
        target.confidence.label      = "person";
        target.confidence.confidence = 0.6F;
        target.box                   = {30 + i * 80, 40, 50, 160};
        result.targets.push_back(target);
    }
    return result;
}

cosmo::AlgDataPtr Data(const VideoFramePtr& frame, const std::string& task = "privacy-task") {
    auto data               = std::make_shared<cosmo::AlgData>();
    data->chanDataDec.frame = frame;
    data->taskId            = task;
    return data;
}

void CaptureAndPublish(cosmo::AlgData& data, const cosmo::DataDetTrackClassify& result) {
    cosmo::CaptureAlarmPrivacySnapshot(data, "detector-A", {"person"}, result);
    cosmo::PublishAlarmPrivacySnapshot(data);
}

}  // namespace

TEST_CASE("Privacy snapshot keeps full-frame targets before task filters", "[alarm][privacy][snapshot]") {
    auto data   = Data(MetadataFrame());
    auto result = Detection(data->chanDataDec.frame, 5);
    // Two targets belong to the alarm ROI; three are outside it, including a filtered target.
    result.targets[0].areaSign.areas.emplace_back();
    result.targets[1].areaSign.areas.emplace_back();
    result.targets[4].bFilter = true;
    CaptureAndPublish(*data, result);
    data->chanDataDetect.detRet = std::make_shared<cosmo::DataDetTrackClassify>(result);
    auto copy                   = cosmo::AlgDataCopy(data);
    result.targets.clear();
    copy->chanDataDetect.detRet->targets.resize(2);

    auto snapshot = cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId);
    REQUIRE(snapshot);
    REQUIRE(snapshot->targets.size() == 5);
    CHECK(snapshot->targets[4].box.x == 350);
    CHECK(snapshot->targets[4].label == "person");
    CHECK(copy->alarmPrivacySnapshot == snapshot);
}

TEST_CASE("Privacy snapshot distinguishes successful empty detection from missing metadata",
          "[alarm][privacy][snapshot]") {
    auto empty = Data(MetadataFrame());
    CaptureAndPublish(*empty, Detection(empty->chanDataDec.frame));
    auto snapshot = cosmo::FindAlarmPrivacySnapshot(empty->chanDataDec.frame, empty->taskId);
    REQUIRE(snapshot);
    CHECK(snapshot->targets.empty());
    REQUIRE(snapshot->labels.size() == 1);
    CHECK(snapshot->labels[0] == "person");

    auto missing = Data(MetadataFrame());
    cosmo::PublishAlarmPrivacySnapshot(*missing);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(missing->chanDataDec.frame, missing->taskId));
}

TEST_CASE("Privacy capture rejects frame identity and dimension mismatches", "[alarm][privacy][snapshot]") {
    auto data   = Data(MetadataFrame());
    auto result = Detection(data->chanDataDec.frame, 1);
    SECTION("frame") {
        ++result.frameIndex;
    }
    SECTION("stream") {
        ++result.streamIndex;
    }
    SECTION("timestamp") {
        ++result.timestamp;
    }
    SECTION("width") {
        ++result.picWidth;
    }
    SECTION("height") {
        ++result.picHeight;
    }
    CaptureAndPublish(*data, result);
    CHECK(data->alarmPrivacyUnavailable);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
}

TEST_CASE("Privacy lookup never substitutes equal timestamps or a mutated frame",
          "[alarm][privacy][snapshot]") {
    auto frame = MetadataFrame();
    auto data  = Data(frame);
    CaptureAndPublish(*data, Detection(frame, 1));
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(MetadataFrame(), data->taskId));
    SECTION("frame") {
        frame->SetFrameIndex(43);
    }
    SECTION("stream") {
        frame->SetStreamIndex(8);
    }
    SECTION("timestamp") {
        frame->SetTimestamp(1001);
    }
    SECTION("width") {
        frame->SetWidth(641);
    }
    SECTION("height") {
        frame->SetHeight(481);
    }
    SECTION("replacement with identical sequence and timestamp") {
        auto replacement = MetadataFrame();
        *frame           = std::move(*replacement);
    }
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(frame, data->taskId));
}

TEST_CASE("Privacy metadata follows retained historical frames without retaining image ownership",
          "[alarm][privacy][snapshot]") {
    auto frame                                        = MetadataFrame();
    std::weak_ptr<cosmo::media::VideoFrame> weakFrame = frame;
    auto data                                         = Data(frame);
    CaptureAndPublish(*data, Detection(frame, 5));
    auto bestFrame = frame;
    data.reset();
    frame.reset();
    // More than ten seconds and several cache sweeps do not expire a still-owned best frame.
    for (int i = 0; i < 130; ++i) {
        auto later = Data(MetadataFrame(100 + i, 20000 + i * 1000));
        CaptureAndPublish(*later, Detection(later->chanDataDec.frame));
    }
    auto snapshot = cosmo::FindAlarmPrivacySnapshot(bestFrame, "privacy-task");
    REQUIRE(snapshot);
    CHECK(snapshot->targets.size() == 5);
    bestFrame.reset();
    CHECK(weakFrame.expired());
    CHECK(snapshot->frame.expired());
}

TEST_CASE("Privacy snapshots are isolated by task even on the same frame", "[alarm][privacy][snapshot]") {
    auto frame  = MetadataFrame();
    auto first  = Data(frame, "task-one");
    auto second = Data(frame, "task-two");
    CaptureAndPublish(*first, Detection(frame, 2));
    CaptureAndPublish(*second, Detection(frame, 5));
    REQUIRE(cosmo::FindAlarmPrivacySnapshot(frame, "task-one"));
    REQUIRE(cosmo::FindAlarmPrivacySnapshot(frame, "task-two"));
    CHECK(cosmo::FindAlarmPrivacySnapshot(frame, "task-one")->targets.size() == 2);
    CHECK(cosmo::FindAlarmPrivacySnapshot(frame, "task-two")->targets.size() == 5);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(frame, "task-three"));
}

TEST_CASE("Privacy cache advances causal detector chains but rejects independent partial branches",
          "[alarm][privacy][snapshot]") {
    auto data  = Data(MetadataFrame());
    auto first = Detection(data->chanDataDec.frame, 2);
    CaptureAndPublish(*data, first);
    auto firstStage = cosmo::AlgDataCopy(data);
    auto next       = Detection(data->chanDataDec.frame);
    cosmo::AiDetectRstEl vehicle;
    vehicle.confidence.label = "vehicle";
    vehicle.box              = {300, 100, 200, 200};
    next.targets.push_back(vehicle);
    cosmo::CaptureAlarmPrivacySnapshot(*data, "detector-B", {"vehicle"}, next);
    cosmo::PublishAlarmPrivacySnapshot(*data);
    cosmo::PublishAlarmPrivacySnapshot(*firstStage);
    auto complete = cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId);
    REQUIRE(complete);
    CHECK(complete->labels.size() == 2);
    CHECK(complete->targets.size() == 3);

    auto branch = cosmo::AlgDataCopy(firstStage);
    cosmo::CaptureAlarmPrivacySnapshot(*branch, "detector-C", {"vehicle"}, next);
    cosmo::PublishAlarmPrivacySnapshot(*branch);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
    cosmo::PublishAlarmPrivacySnapshot(*data);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
}

TEST_CASE("Final missing or unsupported privacy metadata revokes earlier detector results",
          "[alarm][privacy][snapshot]") {
    auto data = Data(MetadataFrame());
    CaptureAndPublish(*data, Detection(data->chanDataDec.frame, 1));
    REQUIRE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
    SECTION("missing final metadata") {
        data->alarmPrivacySnapshot.reset();
    }
    SECTION("unsupported detector stage") {
        cosmo::InvalidateAlarmPrivacySnapshot(*data);
    }
    cosmo::PublishAlarmPrivacySnapshot(*data);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
}

TEST_CASE("Privacy capture cannot certify an uncaptured upstream detector or unknown classes",
          "[alarm][privacy][snapshot]") {
    auto data   = Data(MetadataFrame());
    auto result = Detection(data->chanDataDec.frame);
    SECTION("uncaptured upstream detector") {
        data->chanDataDetect.detRet = std::make_shared<cosmo::DataDetTrackClassify>(result);
        CaptureAndPublish(*data, result);
    }
    SECTION("unknown detector classes") {
        cosmo::CaptureAlarmPrivacySnapshot(*data, "detector-A", {}, result);
        cosmo::PublishAlarmPrivacySnapshot(*data);
    }
    REQUIRE(data->alarmPrivacyUnavailable);
    CHECK_FALSE(cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId));
}

TEST_CASE("Privacy requires every configured detector before an unseen branch can emit images",
          "[alarm][privacy][snapshot][topology]") {
    auto data = Data(MetadataFrame());
    CaptureAndPublish(*data, Detection(data->chanDataDec.frame));
    REQUIRE(data->alarmPrivacySnapshot);
    CHECK(cosmo::HasAlarmPrivacyDetectors(*data->alarmPrivacySnapshot, {"detector-A"}));
    CHECK_FALSE(cosmo::HasAlarmPrivacyDetectors(*data->alarmPrivacySnapshot, {"detector-A", "detector-B"}));
    CHECK_FALSE(cosmo::HasAlarmPrivacyDetectors(*data->alarmPrivacySnapshot, {"detector"}));
    CHECK_FALSE(cosmo::HasAlarmPrivacyDetectors(*data->alarmPrivacySnapshot, {""}));

    // A serial second detector is now known to have run successfully, including zero targets.
    cosmo::CaptureAlarmPrivacySnapshot(*data, "detector-B", {"vehicle"}, Detection(data->chanDataDec.frame));
    cosmo::PublishAlarmPrivacySnapshot(*data);
    auto complete = cosmo::FindAlarmPrivacySnapshot(data->chanDataDec.frame, data->taskId);
    REQUIRE(complete);
    CHECK(cosmo::HasAlarmPrivacyDetectors(*complete, {"detector-A", "detector-B"}));
    CHECK_FALSE(cosmo::HasAlarmPrivacyDetectors(*complete, {"detector-A", "unsupported-detector"}));
}
