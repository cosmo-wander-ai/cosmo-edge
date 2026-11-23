#pragma once

#include <functional>

namespace cosmo {
// All hooks run without the channel data lock. Caller owns the separate
// lifecycle transition and serializes other starts/stops.
struct ChannelStartOps {
    std::function<bool()> configure;
    std::function<bool()> start_base;
    std::function<bool()> register_queue;
    std::function<bool()> start_decoder;
    std::function<bool()> start_demux;
    std::function<bool()> resume_existing;
    std::function<void()> stop_demux;
    std::function<void()> stop_decoder;
    std::function<void()> unregister_queue;
    std::function<void()> stop_base;
};

inline bool RunChannelStartTransaction(bool already_started, const ChannelStartOps& ops) {
    bool base_attempted = false, base = false, queue = false, decoder = false;
    bool queue_attempted = false, decoder_attempted = false, demux_attempted = false;
    bool success = false;
    try {
        if (!already_started && !ops.configure())
            return false;
        base_attempted = true;
        base           = ops.start_base();
        if (base) {
            if (already_started)
                success = ops.resume_existing();
            else {
                queue_attempted = true;
                queue           = ops.register_queue();
                if (queue) {
                    decoder_attempted = true;
                    decoder           = ops.start_decoder();
                    if (decoder) {
                        demux_attempted = true;
                        success         = ops.start_demux();
                    }
                }
            }
        }
    } catch (...) {
        success = false;
    }
    if (!success && !already_started) {
        if (demux_attempted) {
            try {
                ops.stop_demux();
            } catch (...) {
            }
        }
        if (decoder_attempted) {
            try {
                ops.stop_decoder();
            } catch (...) {
            }
        }
        if (queue_attempted) {
            try {
                ops.unregister_queue();
            } catch (...) {
            }
        }
        if (base_attempted) {
            try {
                ops.stop_base();
            } catch (...) {
            }
        }
    }
    return success;
}
}  // namespace cosmo
