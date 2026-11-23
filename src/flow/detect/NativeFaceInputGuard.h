#pragma once

#include <cstddef>

namespace cosmo::native_face {

// This is a routing decision, not model or frame authorization. The caller
// must derive selection from the protected startup selector and compute both
// frame validity and registration identity from the actual queued data.
enum class InputSelection { NotSelected, Selected, Invalid };
enum class InputRoute { Host, Native, Reject };

inline InputRoute DecideInputRoute(InputSelection selection, std::size_t batch_size,
                                   std::size_t native_envelopes, bool frame_valid,
                                   bool registration_matches) noexcept {
    switch (selection) {
        case InputSelection::NotSelected:
            // Keep the established host path for other models, but never
            // reinterpret native data as a host frame.
            return native_envelopes == 0 ? InputRoute::Host : InputRoute::Reject;
        case InputSelection::Selected:
            // The currently supported route is a single task and frame, with
            // its registered detector/queue capability. Witness and lease
            // validation still happen in the native entry before submission.
            return batch_size == 1 && native_envelopes == 1 && frame_valid && registration_matches
                       ? InputRoute::Native
                       : InputRoute::Reject;
        case InputSelection::Invalid:
            return InputRoute::Reject;
    }
    return InputRoute::Reject;
}

}  // namespace cosmo::native_face
