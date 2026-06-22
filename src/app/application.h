#pragma once
#include <string>

namespace cosmo::app {

class Application {
public:
    explicit Application(std::string name);
    ~Application() = default;

    void run(const char* base_dir);

private:
    std::string app_name_;
};

}  // namespace cosmo::app
