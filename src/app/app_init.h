#pragma once

namespace cosmo::app {

void SwDevicePreInit();
void SwDeviceInit();
void SwDeviceRun();  // Blocking — runs the HTTP server event loop.
void SwDeviceDestroy();

}  // namespace cosmo::app
