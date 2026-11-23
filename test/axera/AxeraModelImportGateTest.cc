#include <cassert>
#include <string>

#include "infer/BmodelTool.h"
int main() {
    const auto info = cosmo::BmodelTool::GetBmodelInfo("/ignored/model.axmodel");
    assert(!info.valid);
    assert(info.file_path == "/ignored/model.axmodel");
    assert(!info.error_msg.empty() && info.error_msg != "SDK_NOT_AVAILABLE");
    assert(info.networks.empty());
}
