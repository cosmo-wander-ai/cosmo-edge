#pragma once
#include <string>

#include "util/RuntimeIsolation.h"

namespace cosmo::app {

class Application {
public:
    explicit Application(std::string name);
    ~Application() = default;

    // Returns EXIT_SUCCESS after an intentional shutdown and EXIT_FAILURE when
    // initialization or the serving loop terminates with a fatal exception.
    int run(const char* base_dir, const runtime::RuntimeOptions& options = {});

private:
    std::string app_name_;
};

}  // namespace cosmo::app
