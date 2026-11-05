#pragma once

#include "util/RuntimeIsolation.h"

namespace cosmo::app {

void SwDevicePreInit(const runtime::RuntimeOptions& options = {});
void SwDeviceInit(const runtime::RuntimeOptions& options = {});
void SwDeviceRun();  // Blocking — runs the HTTP server event loop.
void SwDeviceDestroy();

}  // namespace cosmo::app
