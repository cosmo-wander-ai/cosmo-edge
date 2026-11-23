#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "flow/detect/AxeraGraphAdmission.h"
#include "flow/detect/AxeraGraphJson.h"
#include "flow/detect/PersonTemplateGraph.h"
#include "flow/detect/TrustedPersonMetadata.h"
#include "service/algorithm/impl/ActionModelParams.h"

using namespace cosmo;
using namespace cosmo::person_template_contract;

static ActionNode Action(const char* id, const char* parent, const char* action, const char* atomic = "") {
    ActionNode node;
    node.flowActionId    = id;
    node.preFlowActionId = parent;
    node.actionId        = action;
    node.atomicCode      = atomic;
    return node;
}

static void Param(ActionNode& node, const char* key, const char* value) {
    MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    node.configObject.params.push_back(param);
}

int main(int argc, char** argv) {
    std::vector<ActionNode> graph{Action("v", "-1", "BA_00001"), Action("d", "v", "AA_00001", "1001003"),
                                  Action("s", "d", "BA_00003"), Action("a", "s", "BA_00004")};
    Param(graph[1], "atomicCode", "1001003");
    Param(graph[3], "alarmProperty", "0");
    Param(graph[3], "custParam.alarmType", "1");
    assert(MatchesActionGraph(graph, "2"));
    assert(MatchesCandidateGraph(graph, "2", "d"));
    assert(!MatchesCandidateGraph(graph, "2", "v"));
    assert(!MatchesCandidateGraph(graph, "2", "missing"));
    assert(!MatchesActionGraph(graph, "1"));
    auto draft = graph;
    draft[1].atomicCode.clear();
    draft[1].configObject.params.clear();
    assert(MatchesCandidateGraph(draft, "3", "d"));
    assert(draft[1].atomicCode.empty());
    assert(!MatchesActionGraph(draft, "3"));
    auto wrong        = graph;
    wrong[2].actionId = "BA_00002";
    assert(!MatchesCandidateGraph(wrong, "2", "d"));
    wrong                    = graph;
    wrong[2].preFlowActionId = "v";
    assert(!MatchesCandidateGraph(wrong, "2", "d"));
    wrong                 = graph;
    wrong[3].flowActionId = "d";
    assert(!MatchesCandidateGraph(wrong, "2", "d"));
    wrong = graph;
    Param(wrong[3], "alarmProperty", "0");
    assert(!MatchesCandidateGraph(wrong, "2", "d"));
    wrong               = graph;
    wrong[1].atomicCode = "9275710";
    assert(!MatchesCandidateGraph(wrong, "2", "d"));

    using cosmo::axera_graph_admission::EvaluateAxera;
    using cosmo::axera_graph_admission::PersonRouteFacts;
    int called           = 0;
    const auto reference = [&](const std::string&) {
        ++called;
        return PersonRouteFacts{true, false, true};
    };
    assert(EvaluateAxera("74169", "2", graph, true, reference, [] { return true; }));
    assert(called == 1);
    assert(!EvaluateAxera("74169", "2", wrong, true, reference, [] { return true; }));
    auto incomplete        = graph;
    incomplete[2].actionId = "BA_00002";
    assert(!EvaluateAxera("74169", "2", incomplete, true, reference, [] { return true; }));
    assert(!EvaluateAxera(
        "74169", "2", graph, true, [](const auto&) { return PersonRouteFacts{}; }, [] { return true; }));
    assert(EvaluateAxera(
        "existing-dedicated", "2", incomplete, true,
        [](const auto&) { return PersonRouteFacts{true, true, false}; }, [] { return true; }));
    auto benchmark                            = graph;
    benchmark[1].atomicCode                   = "9275710";
    benchmark[1].configObject.params[0].value = "9275710";
    assert(!EvaluateAxera("74169", "2", benchmark, true, reference, [] { return true; }));
    assert(EvaluateAxera("930927510", "2", benchmark, true, reference, [] { return true; }));
    assert(!EvaluateAxera("930927510", "2", benchmark, true, reference, [] { return false; }));
    assert(!EvaluateAxera("930927510", "2", benchmark, false, reference, [] { return true; }));
    auto ordinary                            = graph;
    ordinary[1].atomicCode                   = "ordinary-model";
    ordinary[1].configObject.params[0].value = "ordinary-model";
    const auto before                        = called;
    assert(EvaluateAxera("74169", "2", ordinary, true, reference, [] { return true; }));
    assert(called == before);
    auto conflicting = graph;
    Param(conflicting[1], " modelCode ", "1001003");
    assert(!MatchesCandidateGraph(conflicting, "2", "d"));
    assert(!EvaluateAxera("74169", "2", conflicting, true, reference, [] { return true; }));
    const auto escaped = nlohmann::json::parse(R"([{"value":"\u0031\u0030\u0030\u0031\u0030\u0030\u0033"}])");
    assert(cosmo::axera_graph_admission::ContainsControlledModel(escaped));
    assert(cosmo::axera_graph_admission::ContainsControlledModel(nlohmann::json::parse("[9275710]")));
    assert(!cosmo::axera_graph_admission::ContainsControlledModel(
        nlohmann::json::parse(R"([{"value":"ordinary"}])")));

    Model::MsgAtomicModel person;
    person.atomicCode = "1001003";
    person.modelType  = "axera_native_person";
    Model::MsgAtomicModelLabel label;
    label.label      = "0";
    label.class_name = "pedestrian";
    label.threshold  = 0.63f;
    person.labelList.push_back(label);
    std::vector<Model::MsgAtomicModel> models{person};
    assert(CompatibleMetadata(models, 0.5f));
    models[0].labelList[0].class_name = "other";
    assert(!CompatibleMetadata(models, 0.5f));
    models[0].labelList[0].class_name = "pedestrian";
    models[0].labelList[0].threshold  = 0.2f;
    assert(!CompatibleMetadata(models, 0.5f));
    models[0].labelList[0].threshold = 0.63f;
    models[0].modelType              = "detector";
    assert(!CompatibleMetadata(models, 0.5f));

    ActionNode raw = Action("raw", "v", "AA_00001");
    Param(raw, "modelCode", "1001003");
    service::action_model_params::Extract(raw, true);
    assert(raw.atomicCode == "1001003");
    raw.atomicCode.clear();
    service::action_model_params::Extract(raw, false);
    assert(raw.atomicCode.empty());

    assert(argc == 2);
    std::ifstream in(argv[1]);
    assert(in.good());
    const auto doc     = nlohmann::json::parse(in);
    const auto encoded = nlohmann::json::parse(doc.at("algorithmProcessdata").get<std::string>());
    std::vector<ActionNode> archived;
    for (const auto& item : encoded) {
        ActionNode node;
        node.flowActionId    = item.at("flowActionId").get<std::string>();
        node.preFlowActionId = item.at("preFlowActionId").get<std::string>();
        node.actionId        = item.at("actionId").get<std::string>();
        for (const auto& p : item.at("configObject").at("params"))
            Param(node, p.at("key").get<std::string>().c_str(), p.at("value").get<std::string>().c_str());
        service::action_model_params::Extract(node, true);
        archived.push_back(std::move(node));
    }
    const auto category = doc.at("algorithmCategory").is_string()
                              ? doc.at("algorithmCategory").get<std::string>()
                              : doc.at("algorithmCategory").dump();
    assert(MatchesActionGraph(archived, category));
    assert(MatchesCandidateGraph(archived, category, archived[1].flowActionId));
}
