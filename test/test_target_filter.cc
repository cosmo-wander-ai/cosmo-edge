#include "catch_amalgamated.hpp"

#include "flow/target/TargetFilter.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {
class TargetFilterTestPeer {
public:
    static void Apply(TargetFilter& filter, const DataDetTrackClassifyPtr& input) {
        filter.DoFilter(input);
    }
};
}  // namespace cosmo

namespace {
cosmo::MsgDynamicKeyValue Param(const std::string& key, const std::string& value) {
    cosmo::MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    return param;
}

cosmo::ActionNode Action(std::string_view action_id, const std::string& flow_id,
                         std::vector<cosmo::MsgDynamicKeyValue> params) {
    cosmo::ActionNode action;
    action.actionId                 = std::string(action_id);
    action.actionName               = std::string(action_id);
    action.flowActionId             = flow_id;
    action.configObject.params      = std::move(params);
    return action;
}

cosmo::AiDetectRstEl Target(const std::string& label, int width, int height,
                            const std::string& alg_code = "1001") {
    cosmo::AiDetectRstEl target;
    target.confidence.label = label;
    target.algCode          = alg_code;
    target.box.width        = width;
    target.box.height       = height;
    return target;
}

cosmo::DataDetTrackClassifyPtr Input(std::initializer_list<cosmo::AiDetectRstEl> targets) {
    auto input     = std::make_shared<cosmo::DataDetTrackClassify>();
    input->targets = targets;
    return input;
}
}  // namespace

TEST_CASE("category filter works without a size rule", "[target-filter]") {
    auto action = Action(cosmo::BAFilter_Code, "category-node",
                         {Param("categoryFilter.category-node.1001.person.enabled", "1")});
    cosmo::TargetFilter filter("task", action);
    auto input = Input({Target("person", 20, 20), Target("car", 100, 100)});

    cosmo::TargetFilterTestPeer::Apply(filter, input);

    CHECK_FALSE(input->targets[0].bFilter);
    CHECK(input->targets[1].bFilter);
    CHECK(input->targets[1].filterDesc == "Category Filter: label not selected");
}

TEST_CASE("size filter only applies to selected categories", "[target-filter]") {
    auto action = Action(cosmo::BASizeFilter_Code, "size-node",
                         {Param("sizeFilter.size-node.1001.person.side.min", "60")});
    cosmo::TargetFilter filter("task", action);
    auto input = Input(
        {Target("person", 50, 50), Target("person", 60, 60), Target("car", 20, 20)});

    cosmo::TargetFilterTestPeer::Apply(filter, input);

    CHECK(input->targets[0].bFilter);
    CHECK_FALSE(input->targets[1].bFilter);
    CHECK_FALSE(input->targets[2].bFilter);
}

TEST_CASE("category and size filters compose independently", "[target-filter]") {
    auto category_action = Action(cosmo::BAFilter_Code, "category-node",
                                  {Param("categoryFilter.category-node.1001.person.enabled", "1")});
    auto size_action = Action(cosmo::BASizeFilter_Code, "size-node",
                              {Param("sizeFilter.size-node.1001.person.side.min", "60")});
    cosmo::TargetFilter category_filter("task", category_action);
    cosmo::TargetFilter size_filter("task", size_action);
    auto input = Input({Target("person", 50, 50), Target("person", 80, 80), Target("car", 80, 80)});

    cosmo::TargetFilterTestPeer::Apply(category_filter, input);
    cosmo::TargetFilterTestPeer::Apply(size_filter, input);

    CHECK(input->targets[0].bFilter);
    CHECK_FALSE(input->targets[1].bFilter);
    CHECK(input->targets[2].bFilter);
}

TEST_CASE("size filter parameters are scoped to one flow node", "[target-filter]") {
    auto action = Action(cosmo::BASizeFilter_Code, "size-node-a",
                         {Param("sizeFilter.size-node-b.1001.person.side.min", "60")});
    cosmo::TargetFilter filter("task", action);
    auto input = Input({Target("person", 20, 20)});

    cosmo::TargetFilterTestPeer::Apply(filter, input);

    CHECK_FALSE(input->targets[0].bFilter);
}

TEST_CASE("legacy combined filter remains compatible", "[target-filter]") {
    auto action = Action(cosmo::BAFilter_Code, "legacy-node",
                         {Param("filter.person.side.min", "60")});
    cosmo::TargetFilter filter("task", action);
    auto input = Input(
        {Target("person", 50, 50, ""), Target("person", 80, 80, ""), Target("car", 80, 80, "")});

    cosmo::TargetFilterTestPeer::Apply(filter, input);

    CHECK(input->targets[0].bFilter);
    CHECK_FALSE(input->targets[1].bFilter);
    CHECK(input->targets[2].bFilter);
}
