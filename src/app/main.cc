#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "app/application.h"
#include "util/RuntimeIsolation.h"

int main(int argc, char* argv[]) {
    try {
        const auto options =
            cosmo::runtime::ParseRuntimeOptions(std::vector<std::string>(argv + 1, argv + argc));
        if (options.show_help) {
            std::fputs(cosmo::runtime::RuntimeOptionsHelp(), stdout);
            return 0;
        }
        cosmo::app::Application app("Cosmo");
        return app.run("..", options);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Startup refused: %s\n", ex.what());
        return 2;
    }
}
