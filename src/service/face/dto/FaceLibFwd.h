// Forward declarations for FaceLib / Person / FacePic —
// avoids pulling in flow/face/*.h into service interfaces.

#pragma once

#include <memory>

namespace cosmo {
class FaceLib;
class Person;
class FacePic;

using FaceLibPtr = std::shared_ptr<FaceLib>;
using PersonPtr  = std::shared_ptr<Person>;
using FacePicPtr = std::shared_ptr<FacePic>;
}  // namespace cosmo
