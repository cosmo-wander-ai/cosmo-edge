#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "catch_amalgamated.hpp"
#include "service/detail/ServiceRegistry.h"
#include "util/PathUtil.h"

namespace {

bool RegistryIsClean(const char* phase) {
    const auto& registry = cosmo::service::ServiceRegistry::Instance();
    if (registry.GetLifecycleState() == cosmo::service::ServiceRegistry::LifecycleState::kRegistering &&
        registry.Size() == 0) {
        return true;
    }

    std::cerr << "test isolation failure " << phase
              << ": registry state=" << static_cast<int>(registry.GetLifecycleState())
              << " size=" << registry.Size() << '\n';
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (!RegistryIsClean("before Catch session")) {
        return 2;
    }

    const auto suffix       = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto process_root = std::filesystem::temp_directory_path() / ("cosmo-tests-process-" + suffix);
    const auto initial_data_root = (process_root / "data").string();
    const auto initial_app_root  = (process_root / "app").string();
    std::error_code cleanup_error;
    std::filesystem::remove_all(process_root, cleanup_error);
    cosmo::path::OverrideRootPathForTest(initial_data_root, initial_app_root);

    const auto initial_cwd = std::filesystem::current_path();

    const int test_result = Catch::Session().run(argc, argv);
    bool clean            = RegistryIsClean("after Catch session");
    if (cosmo::path::GetBaseDir() != initial_data_root || cosmo::path::GetAppBaseDir() != initial_app_root) {
        std::cerr << "test isolation failure after Catch session: path roots were not restored\n";
        clean = false;
    }
    std::error_code current_path_error;
    const auto final_cwd = std::filesystem::current_path(current_path_error);
    if (current_path_error || final_cwd != initial_cwd) {
        std::cerr << "test isolation failure after Catch session: current directory was not restored\n";
        clean = false;
    }

    std::filesystem::remove_all(process_root, cleanup_error);

    if (test_result != 0) {
        return test_result;
    }
    return clean ? 0 : 2;
}
