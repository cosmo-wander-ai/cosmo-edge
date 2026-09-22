#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/action/AlgActionBase.h"
#include "flow/alarm/AlarmBatch.h"
#include "flow/overview/OverviewRecordBehaviorNoneSenRst.h"
#include "flow/task/TaskBaseParam.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "nlohmann/json.hpp"
#include "service/system/IOverviewConfig.h"
#include "support/ScopedServiceOverride.h"
#include "util/DetectionGeometry.h"

#define private public
#include "flow/alarm/AreaAlarm.h"
#include "flow/sensitivity/PosSaveSensitivity.h"
#include "flow/sensitivity/Sensitivity.h"
#undef private

#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"
#include "flow/sensitivity/SensitivityTypes.h"

namespace cosmo {
namespace {

    class DisabledOverview final : public service::IOverviewConfig {
    public:
        void SetOverviewStructureRecord(bool) override {}
        bool GetOverviewStructureRecord() override {
            return false;
        }
        void SetOverviewStructureFile(bool) override {}
        bool GetOverviewStructureFile() override {
            return false;
        }
        std::string GetTaskOverviewDataPath() override {
            return {};
        }
    };

    AiDetectRstEl MeasuredTarget(int track_id, util::Box box, float angle) {
        AiDetectRstEl target;
        target.box                   = box;
        target.trackId               = track_id;
        target.trackIdInfo           = "track-" + std::to_string(track_id);
        target.confidence.label      = "ship";
        target.confidence.confidence = 0.9f;
        target.oriented_corners =
            util::MakeOrientedQuad(box.x + box.width / 2.0f, box.y + box.height / 2.0f, 8.0f, 4.0f, angle);
        TargetAreaUnit area;
        area.area_id   = "area-1";
        area.area_name = "Area 1";
        target.areaSign.areas.push_back(area);
        return target;
    }

    void RequireCorners(const std::optional<util::Quad>& actual, const std::optional<util::Quad>& expected) {
        REQUIRE(actual.has_value() == expected.has_value());
        if (!expected)
            return;
        for (size_t index = 0; index < expected->size(); ++index) {
            CHECK(actual->at(index).x == expected->at(index).x);
            CHECK(actual->at(index).y == expected->at(index).y);
        }
    }

    TEST_CASE("Saved sensitivity alarm uses detections from its retained frame", "[alarm][geometry][frame]") {
        mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {32 * 32 * 3});
        mem::SetMemoryPoolContext(&pool);
        struct PoolReset {
            ~PoolReset() {
                mem::SetMemoryPoolContext(nullptr);
            }
        } pool_reset;
        DisabledOverview overview;
        test::ScopedServiceOverride<service::IOverviewConfig> overview_registration(overview);

        ActionNode action;
        action.flowActionId = "saved-sensitivity";
        PosSaveSensitivity sensitivity("geometry-task", action);
        sensitivity.params_.pos_sen_hit_count   = 1;
        sensitivity.params_.pos_sen_total_count = 10;
        sensitivity.params_.pos_sen_duration    = 1000;

        const auto original        = MeasuredTarget(7, {2, 3, 12, 10}, 0.0f);
        const auto original_friend = MeasuredTarget(8, {16, 4, 12, 10}, 0.3f);
        auto original_frame =
            std::make_shared<media::VideoFrame>(32, 32, media::PixelFormat::PIXEL_RGB8, 11, 1000);
        auto later_frame =
            std::make_shared<media::VideoFrame>(32, 32, media::PixelFormat::PIXEL_RGB8, 12, 4000);
        REQUIRE(VideoFrameValid(original_frame));
        REQUIRE(VideoFrameValid(later_frame));
        auto data                           = std::make_shared<AlgData>();
        auto detections                     = std::make_shared<DataDetTrackClassify>();
        detections->targets                 = {original};
        detections->targets[0].groupTargets = {8};
        auto tracks                         = std::make_shared<DataDetTrackClassify>();
        tracks->targets                     = {original, original_friend};
        data->SetTaskResult(AlgDataType::TaskDataTrack, tracks);
        data->chanDataDec.frame = original_frame;
        sensitivity.dec_frame_  = original_frame;
        sensitivity.timestamp_  = 1000;
        sensitivity.AddHistory(data, detections);
        sensitivity.CalcSensitity(data);

        auto& retained = sensitivity.map_track_id_status_.at(7);
        REQUIRE(retained.frame == original_frame);
        REQUIRE_FALSE(data->taskDataAlarm.alarmData);

        // The tracked target and its associate move/rotate after the snapshot.
        // The actual update and alarm methods must keep both geometry and API
        // target metadata paired with frame 11, not the latest frame 12.
        detections->targets[0]                       = MeasuredTarget(7, {16, 18, 12, 10}, 0.8f);
        detections->targets[0].confidence.confidence = 0.6f;
        detections->targets[0].groupTargets          = {8};
        tracks->targets[1]                           = MeasuredTarget(8, {3, 19, 12, 10}, 1.0f);
        data->chanDataDec.frame                      = later_frame;
        sensitivity.dec_frame_                       = later_frame;
        sensitivity.timestamp_                       = 4000;
        sensitivity.AddHistory(data, detections);
        sensitivity.CalcSensitity(data);
        sensitivity.HandTrackAlarm(data, retained, "geometry-test");

        REQUIRE(data->chanDataDec.frame == original_frame);
        CHECK(data->chanDataDec.frame->GetFrameIndex() == 11);
        REQUIRE(data->taskDataAlarm.alarmData);
        REQUIRE(data->taskDataAlarm.alarmData->alarms.size() == 1);
        const auto& alarm = data->taskDataAlarm.alarmData->alarms.front();
        CHECK(alarm.box == original.box);
        REQUIRE(alarm.targets.size() == 1);
        CHECK(alarm.targets[0].box.x == original.box.x);
        CHECK(alarm.targets[0].box.y == original.box.y);
        CHECK(alarm.targets[0].confidence == original.confidence.confidence);
        RequireCorners(alarm.targets[0].oriented_corners, original.oriented_corners);
        REQUIRE(alarm.boxs.size() == 2);
        CHECK(alarm.boxs[0].box == original.box);
        RequireCorners(alarm.boxs[0].oriented_corners, original.oriented_corners);
        CHECK(alarm.boxs[1].box == original_friend.box);
        RequireCorners(alarm.boxs[1].oriented_corners, original_friend.oriented_corners);
        const nlohmann::json wire_target = alarm.targets[0];
        CHECK(wire_target["box"]["x"] == original.box.x);
        CHECK(wire_target["orientedCorners"][0]["x"] == original.oriented_corners->at(0).x);

        // A second eligible track has a different saved frame. The one-alarm
        // limit must reject it before it can replace the accepted alarm's image.
        PosSaveSensitivity::TrackIdData other_track;
        other_track.track_id                 = 9;
        other_track.first_timestamp          = 1000;
        other_track.last_timestamp           = 4000;
        other_track.logic_count_can_be_alarm = true;
        other_track.frame                    = later_frame;
        other_track.target                   = MeasuredTarget(9, {6, 17, 12, 10}, 0.6f);
        other_track.frame_target             = other_track.target;
        sensitivity.HandTrackAlarm(data, other_track, "second-track-test");
        REQUIRE(data->taskDataAlarm.alarmData->alarms.size() == 1);
        CHECK(data->chanDataDec.frame == original_frame);
        RequireCorners(data->taskDataAlarm.alarmData->alarms.front().targets[0].oriented_corners,
                       original.oriented_corners);

        // Behavior history describes the current detection independently of the
        // frozen alarm snapshot and preserves its source-pixel geometry too.
        sensitivity.width_  = 32;
        sensitivity.height_ = 32;
        MsgRecPosSaveSensitityTarget recorded;
        sensitivity.TrackData2RecData(retained, recorded);
        CHECK(recorded.aiBox.x == detections->targets[0].box.x);
        RequireCorners(recorded.oriented_corners, detections->targets[0].oriented_corners);
        const nlohmann::json record_json = recorded;
        CHECK(record_json["orientedCorners"][0]["x"] == detections->targets[0].oriented_corners->at(0).x);
        retained.target.oriented_corners.reset();
        sensitivity.TrackData2RecData(retained, recorded);
        CHECK_FALSE(recorded.oriented_corners);
    }

    TEST_CASE("Friend sensitivity alarms preserve the latest measured overlays",
              "[alarm][geometry][friends]") {
        ActionNode action;
        action.flowActionId = "friend-sensitivity";
        Sensitivity sensitivity("geometry-task", action);
        auto input = std::make_shared<DataDetTrackClassify>();
        AiGroupEl group;
        group.groupId       = 7;
        group.groupIdInfo   = "friend-group";
        group.genTarget.box = {2, 3, 24, 16};
        const util::Box common_box{3, 4, 12, 10};
        auto first    = MeasuredTarget(1, common_box, 0.3f);
        auto second   = MeasuredTarget(2, common_box, 1.1f);
        auto ordinary = MeasuredTarget(3, common_box, 0.0f);
        ordinary.oriented_corners.reset();
        group.srcTargets     = {first, second, ordinary};
        input->groupTargets  = {group};
        const auto timestamp = std::chrono::steady_clock::now();
        sensitivity.AddGroupHistory(input, timestamp);

        // History must own its snapshot even when upstream reuses detections.
        input->groupTargets[0].srcTargets[0].oriented_corners.reset();
        auto& history = sensitivity.m_mapTrackIdStatus.at(7);
        DataAlarmUnit initial;
        sensitivity.FillAlarmDataTrackId(initial, history);
        REQUIRE(initial.boxs.size() == 3);
        REQUIRE(initial.friends.size() == 3);
        CHECK(initial.box == group.genTarget.box);
        RequireCorners(initial.boxs[0].oriented_corners, first.oriented_corners);
        RequireCorners(initial.boxs[1].oriented_corners, second.oriented_corners);
        CHECK_FALSE(initial.boxs[2].oriented_corners);
        for (size_t i = 0; i < initial.boxs.size(); ++i) {
            CHECK(initial.boxs[i].box == common_box);
            CHECK(initial.friends[i] == common_box);
        }

        // The next group observation reorders members and drops oriented
        // geometry on one member; neither old order nor old corners may leak.
        input->groupTargets[0].srcTargets    = {second, ordinary};
        input->groupTargets[0].genTarget.box = {4, 5, 20, 14};
        sensitivity.AddGroupHistory(input, timestamp + std::chrono::milliseconds(100));
        DataAlarmUnit latest;
        sensitivity.FillAlarmDataTrackId(latest, history);
        REQUIRE(latest.boxs.size() == 2);
        REQUIRE(latest.friends.size() == 2);
        CHECK(latest.box == input->groupTargets[0].genTarget.box);
        RequireCorners(latest.boxs[0].oriented_corners, second.oriented_corners);
        CHECK_FALSE(latest.boxs[1].oriented_corners);
        for (size_t i = 0; i < latest.boxs.size(); ++i) {
            CHECK(latest.boxs[i].box == latest.friends[i]);
        }
    }

    TEST_CASE("Area count reports preserve per-target measured geometry", "[alarm][geometry][count]") {
        ActionNode action;
        action.flowActionId = "area-count";
        AreaAlarm count("geometry-task", action);
        count.params_.area_duration = 1000;
        count.report_time_point_    = std::chrono::steady_clock::now() - std::chrono::seconds(2);
        auto input                  = std::make_shared<DataDetTrackClassify>();
        auto oriented               = MeasuredTarget(1, {3, 4, 12, 10}, 0.0f);
        auto ordinary               = MeasuredTarget(2, {3, 4, 12, 10}, 0.0f);
        ordinary.oriented_corners.reset();
        auto filtered    = MeasuredTarget(3, {3, 4, 12, 10}, 0.8f);
        filtered.bFilter = true;
        input->targets   = {oriented, ordinary, filtered};
        auto data        = std::make_shared<AlgData>();

        count.HandAreaTargetCountReport(data, input);

        REQUIRE(data->taskDataAlarm.alarmData);
        REQUIRE(data->taskDataAlarm.alarmData->alarms.size() == 1);
        const auto& alarm = data->taskDataAlarm.alarmData->alarms.front();
        REQUIRE(alarm.targets.size() == 2);
        REQUIRE(alarm.boxs.size() == 2);
        RequireCorners(alarm.targets[0].oriented_corners, oriented.oriented_corners);
        RequireCorners(alarm.boxs[0].oriented_corners, oriented.oriented_corners);
        CHECK_FALSE(alarm.targets[1].oriented_corners);
        CHECK_FALSE(alarm.boxs[1].oriented_corners);
    }

    TEST_CASE("Alarm batching preserves distinct overlays sharing the same AABB",
              "[alarm][geometry][batch]") {
        const util::Box common_box{3, 4, 12, 10};
        auto first  = MeasuredTarget(1, common_box, 0.3f);
        auto second = MeasuredTarget(2, common_box, 1.1f);
        std::deque<DataAlarmUnit> alarms(2);
        alarms[0].box = common_box;
        alarms[0].boxs.push_back(MakeAlarmBox(first));
        alarms[0].targets.push_back(MakeOnEventsTarget(first));
        alarms[1].box = common_box;
        alarms[1].boxs.push_back(MakeAlarmBox(second));
        alarms[1].boxs.push_back(common_box);  // Existing AABB-only producers remain supported.
        alarms[1].targets.push_back(MakeOnEventsTarget(second));

        const auto merged = alarm::MergeAlarmBatch(alarms, {1, 0});

        REQUIRE(merged.boxs.size() == 3);
        RequireCorners(merged.boxs[0].oriented_corners, second.oriented_corners);
        CHECK_FALSE(merged.boxs[1].oriented_corners);
        RequireCorners(merged.boxs[2].oriented_corners, first.oriented_corners);
        for (const auto& overlay : merged.boxs)
            CHECK(overlay.box == common_box);
    }

}  // namespace
}  // namespace cosmo
