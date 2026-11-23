#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "media/VideoDecoderAxera.h"
#include "nn/device/axera/axera_face_native_runner.h"
#include "nn/device/axera/facelandmark/JpegAdmission.h"
#include "nn/device/axera/facelandmark/NativeJfifFaceExtractor.h"

namespace cosmo::nn::axera {
// Compressed baseline JFIF is the only accepted input. Decoded planes stay in
// VDEC memory and are consumed by the NPU; this primitive does not annotate.
inline RawYuvResult ExtractJfifFace(const std::uint8_t* jpeg, std::size_t size,
                                    const std::shared_ptr<const face_binding::Capability>& capability,
                                    float threshold) {
    auto fail = [](RawYuvError error) { return RawYuvResult{error, {}}; };
    if (!capability || !face_landmark_candidate::AdmitJpeg601Full(jpeg, size))
        return fail(RawYuvError::InvalidResources);
    auto decoder = std::make_shared<media::VideoDecoderAxera>(0);
    decoder->SetCodecType(media::VideoCodecType::kMjpeg, 1920, 1080);
    decoder->RequireNativeFrames();
    if (!decoder->Open())
        return fail(RawYuvError::InvalidResources);
    if (!decoder->SendPacket(jpeg, size, 1)) {
        helmet_candidate::QuarantineJfifDecoder(std::move(decoder));
        return fail(RawYuvError::DeviceCompletionUnknown);
    }
    media::NativeVideoBufferPtr frame;
    try {
        for (int n = 0; n < 20 && !frame; ++n)
            frame = decoder->GetNativeFrame();
    } catch (...) {
        helmet_candidate::QuarantineJfifDecoder(std::move(decoder));
        return fail(RawYuvError::DeviceCompletionUnknown);
    }
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame)) {
        helmet_candidate::QuarantineJfifDecoder(std::move(decoder));
        return fail(RawYuvError::DeviceCompletionUnknown);
    }
    auto result = FaceNativeRunner::RunJfif(capability, frame, threshold);
    if (result.error == RawYuvError::None && result.face.error == FaceResultError::None)
        LOG_INFO("AX JFIF face native witness and NPU RunSync completed; detections={}",
                 result.face.objects.size());
    if (result.error == RawYuvError::DeviceCompletionUnknown || result.error == RawYuvError::Quarantined) {
        helmet_candidate::QuarantineJfifDecoder(std::move(decoder));
        return result;
    }
    frame.reset();
    if (!decoder->PrepareNativeInput() || !decoder->Close()) {
        helmet_candidate::QuarantineJfifDecoder(std::move(decoder));
        return fail(RawYuvError::DeviceCompletionUnknown);
    }
    return result;
}
}  // namespace cosmo::nn::axera
#endif
