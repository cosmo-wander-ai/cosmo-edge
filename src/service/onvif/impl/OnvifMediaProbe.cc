#include <cmath>
#include <stdexcept>

#include "media/VideoDemuxer.h"
#include "service/onvif/impl/OnvifProtocol.h"

namespace cosmo::service::onvif {
nlohmann::json ProbeStreamMetadata(const std::string& uri, std::chrono::steady_clock::time_point deadline,
                                   const std::atomic<bool>* running) {
    media::VideoDemuxer demux;
    demux.SetIoDeadline(deadline, running);
    if (demux.StopRequested())
        throw std::runtime_error("timeout");
    demux.SetFile(uri);
    // Reuse TCP transport, URL normalization/redaction, codec detection and RAII
    // cleanup. No decoder, channel, preview lease or persistent stream is created.
    const auto opened = demux.OpenStream(false, 5);
    if (demux.StopRequested())
        throw std::runtime_error("timeout");
    if (opened == util::ErrorEnum::DemuxOpenStreamUnauthorized)
        throw std::runtime_error("rtsp_unauthorized");
    if (opened != util::ErrorEnum::Success)
        throw std::runtime_error("stream_probe_failed");
    const auto found = demux.FindStream();
    if (demux.StopRequested())
        throw std::runtime_error("timeout");
    if (found == util::ErrorEnum::VideoFormatNotSupport)
        throw std::runtime_error("unsupported_stream");
    if (found != util::ErrorEnum::Success || demux.GetWidth() <= 0 || demux.GetHeight() <= 0)
        throw std::runtime_error("stream_probe_failed");
    const auto type  = demux.GetEncodeType();
    const auto codec = type == media::VideoCodecType::kH264   ? "H264"
                       : type == media::VideoCodecType::kH265 ? "H265"
                                                              : "MJPEG";
    const auto fps   = demux.GetFPS();
    return {{"codec", codec},
            {"width", demux.GetWidth()},
            {"height", demux.GetHeight()},
            {"fps", std::isfinite(fps) && fps > 0 ? fps : 0}};
}
}  // namespace cosmo::service::onvif
