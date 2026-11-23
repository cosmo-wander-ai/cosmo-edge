#pragma once

#include "flow/detect/NativeFaceActionGuard.h"
#include "flow/detect/PersonTemplateGraph.h"

#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/detect/NativeRequiredConfiguration.h"
#include "flow/detect/TrustedBenchmarkPerson.h"
#include "flow/detect/TrustedPersonMetadata.h"
#include "flow/detect/TrustedPersonReference.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelQuery.h"
#endif

namespace cosmo::axera_graph_admission {
struct PersonRouteFacts {
    bool compiled{false};
    bool exact_scene{false};
    bool reusable_reference{false};
    bool metadata_compatible{true};
};

// Kept independent of filesystem access so graph decisions can be checked
// offline; production callers obtain these facts from the trusted loader.
template <typename ResolvePerson, typename ResolveBenchmark>
inline bool EvaluateAxera(const std::string& scene, const std::string& category,
                          const std::vector<ActionNode>& actions, bool benchmark_compiled,
                          ResolvePerson resolve_person, ResolveBenchmark resolve_benchmark) {
    bool needs_person    = false;
    bool needs_benchmark = false;
    for (const auto& action : actions) {
        if (action.actionId != "AA_00001")
            continue;
        if (!native_face::ConsistentStrictAction("1001003", action.atomicCode, action) ||
            !native_face::ConsistentStrictAction("9275710", action.atomicCode, action))
            return false;
        if (action.atomicCode == "9275710") {
            if (!benchmark_compiled || (scene != "930927510" && scene != "930007463"))
                return false;
            needs_benchmark = true;
        }
        if (action.atomicCode == "1001003")
            needs_person = true;
    }
    if (needs_benchmark && !resolve_benchmark())
        return false;
    if (!needs_person)
        return true;
    const auto route = resolve_person(scene);
    return route.compiled &&
           (route.exact_scene || (route.reusable_reference && route.metadata_compatible &&
                                  person_template_contract::MatchesActionGraph(actions, category)));
}

// Only the two models whose native policy is covered by this candidate are
// inspected. All other platforms and native/dedicated models retain existing
// admission. This precheck does not replace runtime binding or frame checks.
inline bool Admits(const std::string& scene, const std::string& category,
                   const std::vector<ActionNode>& actions) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    const bool benchmark_compiled = native_face::ProcessRequires("9275710");
#else
    constexpr bool benchmark_compiled = false;
#endif
    try {
        return EvaluateAxera(
            scene, category, actions, benchmark_compiled,
            [](const std::string& scene_id) {
#ifdef COSMO_AXERA_PERSON_ROUTE_CANDIDATE
                if (!native_face::ProcessRequires("1001003"))
                    return PersonRouteFacts{};
                const auto routes = person_template_contract::ResolveTrustedPersonRoutes(scene_id);
                if (routes.exact_scene)
                    return PersonRouteFacts{true, true, routes.reusable_reference, true};
                if (!routes.reusable_reference)
                    return PersonRouteFacts{true, false, false, false};
                auto& registry = service::ServiceRegistry::Instance();
                if (!registry.Has<service::IModelQuery>())
                    return PersonRouteFacts{true, false, true, false};
                const auto models = registry.Get<service::IModelQuery>().QueryAtomicModels("", "", "");
                return PersonRouteFacts{true, false, true,
                                        person_template_contract::CompatibleMetadata(
                                            models, routes.minimum_confidence) != nullptr};
#else
                (void)scene_id;
                return PersonRouteFacts{};
#endif
            },
            [] {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
                return VerifyTrustedBenchmarkPerson();
#else
                return false;
#endif
            });
    } catch (...) {
        return false;
    }
#else
    (void)scene;
    (void)category;
    (void)actions;
    return true;
#endif
}
}  // namespace cosmo::axera_graph_admission
