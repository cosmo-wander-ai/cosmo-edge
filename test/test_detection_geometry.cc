#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "util/DetectionGeometry.h"
#include "util/GeometricPos.h"
#include "util/dto/ClientMsgEvent.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/TaskAreaTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
namespace {

    using OsdLine = std::pair<util::Point, util::Point>;

    bool HasSegment(const std::vector<OsdLine>& lines, int x1, int y1, int x2, int y2) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
            return (line.first.x == x1 && line.first.y == y1 && line.second.x == x2 && line.second.y == y2) ||
                   (line.first.x == x2 && line.first.y == y2 && line.second.x == x1 && line.second.y == y1);
        });
    }

    void RequireSameCorners(const util::Quad& actual, const util::Quad& expected) {
        for (size_t i = 0; i < expected.size(); ++i) {
            REQUIRE(actual[i].x == expected[i].x);
            REQUIRE(actual[i].y == expected[i].y);
        }
    }

    template <typename Target>
    Target GeometryTarget() {
        Target target{};
        // Source coordinates may be fractional and outside the image. The wire
        // geometry must retain them instead of clipping or normalizing them.
        target.oriented_corners =
            util::Quad{{{-10.25f, 5.5f}, {39.75f, -2.25f}, {50.25f, 70.5f}, {0.5f, 78.25f}}};
        if constexpr (std::is_same_v<Target, MsgTarget> ||
                      std::is_same_v<Target, MsgRecPosSaveSensitityTarget>) {
            target.box   = {-0.11, -0.03, 0.62, 0.82};
            target.aiBox = {-11, -3, 62, 82};
        } else {
            target.box = {-11, -3, 62, 82};
        }
        return target;
    }

    TEST_CASE("Quad OSD clips crossing edges without inventing image-border edges", "[geometry][osd]") {
        SECTION("Both endpoints can be outside while the horizontal edge is visible") {
            const util::Quad corners{{{-50.0f, 20.0f}, {150.0f, 20.0f}, {150.0f, 60.0f}, {-50.0f, 60.0f}}};
            const auto before = corners;
            const auto lines  = util::GetQuadOsdLines(corners, 100, 80);
            REQUIRE(lines.size() == 2);
            REQUIRE(HasSegment(lines, 0, 20, 99, 20));
            REQUIRE(HasSegment(lines, 0, 60, 99, 60));
            RequireSameCorners(corners, before);
        }

        SECTION("Oblique edges intersect the boundary along their original slope") {
            const util::Quad corners{{{-20.0f, 0.0f}, {120.0f, 70.0f}, {120.0f, 90.0f}, {-20.0f, 20.0f}}};
            const auto lines = util::GetQuadOsdLines(corners, 100, 80);
            REQUIRE(lines.size() == 2);
            REQUIRE(HasSegment(lines, 0, 10, 99, 60));
            REQUIRE(HasSegment(lines, 0, 30, 98, 79));
            for (const auto& line : lines) {
                for (const auto& point : {line.first, line.second}) {
                    REQUIRE(point.x >= 0);
                    REQUIRE(point.x < 100);
                    REQUIRE(point.y >= 0);
                    REQUIRE(point.y < 80);
                }
            }
        }

        SECTION("An enclosing polygon with all edges outside has no visible outline") {
            const util::Quad corners{
                {{-20.0f, -20.0f}, {120.0f, -20.0f}, {120.0f, 100.0f}, {-20.0f, 100.0f}}};
            REQUIRE(util::GetQuadOsdLines(corners, 100, 80).empty());
        }
    }

    TEST_CASE("Quad OSD supports zero and right-angle rotations", "[geometry][osd]") {
        SECTION("Zero degrees remains a measured quadrilateral") {
            const auto corners = util::MakeOrientedQuad(50.0f, 40.0f, 40.0f, 20.0f, 0.0f);
            const auto lines   = util::GetQuadOsdLines(corners, 100, 80);
            REQUIRE(lines.size() == 4);
            REQUIRE(HasSegment(lines, 30, 30, 70, 30));
            REQUIRE(HasSegment(lines, 70, 30, 70, 50));
            REQUIRE(HasSegment(lines, 70, 50, 30, 50));
            REQUIRE(HasSegment(lines, 30, 50, 30, 30));
        }

        SECTION("Ninety degrees exchanges the horizontal and vertical extents") {
            const auto corners = util::MakeOrientedQuad(50.0f, 40.0f, 40.0f, 20.0f, std::acos(-1.0f) / 2.0f);
            const auto lines   = util::GetQuadOsdLines(corners, 100, 80);
            REQUIRE(lines.size() == 4);
            REQUIRE(HasSegment(lines, 40, 20, 60, 20));
            REQUIRE(HasSegment(lines, 60, 20, 60, 60));
            REQUIRE(HasSegment(lines, 60, 60, 40, 60));
            REQUIRE(HasSegment(lines, 40, 60, 40, 20));
        }
    }

    TEST_CASE("Quad OSD omits invisible and invalid geometry", "[geometry][osd]") {
        const auto inside  = util::MakeOrientedQuad(50.0f, 40.0f, 40.0f, 20.0f, 0.0f);
        const auto outside = util::MakeOrientedQuad(160.0f, 40.0f, 40.0f, 20.0f, 0.0f);
        REQUIRE(util::GetQuadOsdLines(outside, 100, 80).empty());
        for (const auto dimensions : {std::pair<int, int>{0, 80}, {-1, 80}, {100, 0}, {100, -1}}) {
            CAPTURE(dimensions.first, dimensions.second);
            REQUIRE(util::GetQuadOsdLines(inside, dimensions.first, dimensions.second).empty());
        }
        for (const float invalid :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity()}) {
            auto corners = inside;
            corners[2].x = invalid;
            REQUIRE(util::GetQuadOsdLines(corners, 100, 80).empty());
            corners      = inside;
            corners[1].y = invalid;
            REQUIRE(util::GetQuadOsdLines(corners, 100, 80).empty());
        }
    }

    TEMPLATE_TEST_CASE("Detection JSON preserves measured pixel corners and the existing box fields",
                       "[geometry][dto]", MsgPTaskTarget, MsgTarget, CMsgOnEventsTarget,
                       MsgRecPosSaveSensitityTarget) {
        auto target                  = GeometryTarget<TestType>();
        const nlohmann::json encoded = target;
        REQUIRE(encoded.contains("orientedCorners"));
        REQUIRE(encoded["orientedCorners"].size() == 4);
        REQUIRE_FALSE(encoded.contains("angle"));
        REQUIRE_FALSE(encoded.contains("oriented_corners"));
        REQUIRE(encoded["orientedCorners"][0]["x"].template get<float>() == -10.25f);

        const auto decoded = nlohmann::json::parse(encoded.dump()).template get<TestType>();
        REQUIRE(decoded.oriented_corners.has_value());
        RequireSameCorners(*decoded.oriented_corners, *target.oriented_corners);
        const nlohmann::json reencoded = decoded;
        REQUIRE(reencoded["box"] == encoded["box"]);
        if constexpr (std::is_same_v<TestType, MsgTarget> ||
                      std::is_same_v<TestType, MsgRecPosSaveSensitityTarget>)
            REQUIRE(reencoded["aiBox"] == encoded["aiBox"]);

        target.oriented_corners           = util::MakeOrientedQuad(50.0f, 40.0f, 40.0f, 20.0f, 0.0f);
        const nlohmann::json zero_degrees = target;
        REQUIRE(zero_degrees.contains("orientedCorners"));
        const auto zero_roundtrip = zero_degrees.template get<TestType>();
        REQUIRE(zero_roundtrip.oriented_corners.has_value());
        RequireSameCorners(*zero_roundtrip.oriented_corners, *target.oriented_corners);
    }

    TEMPLATE_TEST_CASE("Legacy detection JSON clears previously stored oriented geometry", "[geometry][dto]",
                       MsgPTaskTarget, MsgTarget, CMsgOnEventsTarget, MsgRecPosSaveSensitityTarget) {
        for (const bool explicit_null : {false, true}) {
            CAPTURE(explicit_null);
            auto target                = GeometryTarget<TestType>();
            nlohmann::json reused_json = target;
            auto legacy                = reused_json;
            if (explicit_null)
                legacy["orientedCorners"] = nullptr;
            else
                legacy.erase("orientedCorners");
            REQUIRE_NOTHROW(legacy.get_to(target));
            REQUIRE_FALSE(target.oriented_corners.has_value());

            // Reusing an output object must also remove its previous wire field.
            to_json(reused_json, target);
            REQUIRE_FALSE(reused_json.contains("orientedCorners"));
            REQUIRE(reused_json["box"] == legacy["box"]);
        }
    }

    TEMPLATE_TEST_CASE("Detection JSON rejects malformed and non-finite oriented coordinates",
                       "[geometry][dto]", MsgPTaskTarget, MsgTarget, CMsgOnEventsTarget,
                       MsgRecPosSaveSensitityTarget) {
        auto target                                = GeometryTarget<TestType>();
        const nlohmann::json valid                 = target;
        const auto valid_points                    = valid["orientedCorners"];
        std::vector<nlohmann::json> invalid_points = {
            42,
            "quadrilateral",
            nlohmann::json::object(),
            nlohmann::json::array(),
            nlohmann::json::array({valid_points[0], valid_points[1], valid_points[2]}),
            nlohmann::json::array(
                {valid_points[0], valid_points[1], valid_points[2], valid_points[3], valid_points[0]}),
            nlohmann::json::array({{0, 0}, {1, 0}, {1, 1}, {0, 1}}),
        };
        auto missing_coordinate = valid_points;
        missing_coordinate[1].erase("x");
        invalid_points.push_back(missing_coordinate);
        auto null_point = valid_points;
        null_point[2]   = nullptr;
        invalid_points.push_back(null_point);
        for (const nlohmann::json coordinate :
             {nlohmann::json("1.5"), nlohmann::json(true), nlohmann::json(nullptr), nlohmann::json(1.0e100),
              nlohmann::json(std::numeric_limits<double>::quiet_NaN()),
              nlohmann::json(std::numeric_limits<double>::infinity())}) {
            auto points    = valid_points;
            points[0]["x"] = coordinate;
            invalid_points.push_back(points);
        }
        for (const auto& points : invalid_points) {
            auto malformed               = valid;
            malformed["orientedCorners"] = points;
            REQUIRE_THROWS(malformed.get_to(target));
        }

        for (const float invalid :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity()}) {
            target                          = GeometryTarget<TestType>();
            (*target.oriented_corners)[3].y = invalid;
            nlohmann::json encoded;
            REQUIRE_THROWS(to_json(encoded, target));
        }
    }

    TEST_CASE("Recorded video metadata reads and rewrites legacy rectangles without stale corners",
              "[geometry][dto][mp4-record]") {
        const nlohmann::json legacy = {
            {"index", 12},
            {"color", 1},
            {"rects", {{{"xRatio", 0.1}, {"yRatio", 0.2}, {"wRatio", 0.3}, {"hRatio", 0.4}}}}};
        auto frame = legacy.get<MsgAlarmVideoOverviewFrame>();
        REQUIRE(frame.rects.size() == 1);
        CHECK_FALSE(frame.rects[0].oriented_corners);
        CHECK(frame.sourceWidth == 0);
        CHECK(frame.sourceHeight == 0);
        CHECK(nlohmann::json(frame) == legacy);

        frame.sourceWidth               = 1920;
        frame.sourceHeight              = 1080;
        frame.rects[0].oriented_corners = util::MakeOrientedQuad(50.25f, 40.5f, 40.0f, 20.0f, 0.0f);
        nlohmann::json reused           = frame;
        REQUIRE(reused["rects"][0].contains("orientedCorners"));
        const auto restored = reused.get<MsgAlarmVideoOverviewFrame>();
        RequireSameCorners(*restored.rects[0].oriented_corners, *frame.rects[0].oriented_corners);
        auto old_rect = legacy["rects"][0].get<MsgRect>();
        CHECK(old_rect.x == frame.rects[0].x);
        // Old consumers can still read the extended rectangle's ratio keys.
        CHECK(reused["rects"][0].get<MsgRect>().width == old_rect.width);

        legacy.get_to(frame);
        CHECK(frame.sourceWidth == 0);
        CHECK(frame.sourceHeight == 0);
        CHECK_FALSE(frame.rects[0].oriented_corners);
        to_json(reused, frame);
        CHECK(reused == legacy);
    }

}  // namespace
}  // namespace cosmo
