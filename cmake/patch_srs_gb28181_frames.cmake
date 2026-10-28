cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()

set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
set(header_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.hpp")
file(READ "${source_file}" source)
file(READ "${header_file}" header)
if(source MATCHES "COSMO_GB_ACCESS_UNIT_V1")
    return()
endif()

function(replace_once variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "SRS GB access-unit patch anchor missing: ${before}")
    endif()
    string(SUBSTRING "${${variable}}" ${position} -1 suffix)
    string(LENGTH "${before}" before_length)
    string(SUBSTRING "${suffix}" ${before_length} -1 remainder)
    string(FIND "${remainder}" "${before}" second_position)
    if(NOT second_position EQUAL -1)
        message(FATAL_ERROR "SRS GB access-unit patch anchor is ambiguous: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

# The upstream v7.0.186 fix groups every slice of one PS video message into
# one FLV video message. Keep this focused backport on the 6.0 RTMP API.
replace_once(source [=[srs_error_t SrsGbMuxer::mux_h264(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    // send each frame.
    while (!avs->empty()) {]=] [=[srs_error_t SrsGbMuxer::mux_h264(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    // COSMO_GB_ACCESS_UNIT_V1: one PS video message is one access unit.
    std::string ibps;
    bool keyframe = false;
    while (!avs->empty()) {]=])

replace_once(source [=[        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("Muxer: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }

    return err;
}]=] [=[        // A multi-slice picture must be sent as one FLV video message.
        std::string ibp;
        if ((err = avc_->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
            return srs_error_wrap(err, "mux frame");
        }
        ibps.append(ibp);
        if (nt == SrsAvcNaluTypeIDR) {
            keyframe = true;
        }
    }

    if (!ibps.empty() &&
        (err = write_h264_ipb_frame(ibps, keyframe, dts, pts)) != srs_success) {
        return srs_error_wrap(err, "write frame");
    }
    return err;
}]=])

replace_once(source [=[srs_error_t SrsGbMuxer::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)]=]
    [=[srs_error_t SrsGbMuxer::write_h264_ipb_frame(const std::string& ibps, bool keyframe, uint32_t dts, uint32_t pts)]=])
replace_once(source [=[    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);

    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    std::string ibp;
    if ((err = avc_->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux frame");
    }]=] [=[    SrsVideoAvcFrameType frame_type = keyframe ? SrsVideoAvcFrameTypeKeyFrame : SrsVideoAvcFrameTypeInterFrame;]=])
replace_once(source [=[avc_->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)]=]
    [=[avc_->mux_avc2flv(ibps, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)]=])

replace_once(source [=[srs_error_t SrsGbMuxer::mux_h265(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    // send each frame.
    while (!avs->empty()) {]=] [=[srs_error_t SrsGbMuxer::mux_h265(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    std::string ipbs;
    bool keyframe = false;
    while (!avs->empty()) {]=])

replace_once(source [=[        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("Muxer: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h265_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }

    return err;
}]=] [=[        std::string ipb;
        if ((err = hevc_->mux_ipb_frame(frame, frame_size, ipb)) != srs_success) {
            return srs_error_wrap(err, "hevc mux ipb frame");
        }
        ipbs.append(ipb);
        if (nt >= SrsHevcNaluType_CODED_SLICE_BLA && nt <= SrsHevcNaluType_RESERVED_23) {
            keyframe = true;
        }
    }

    if (!ipbs.empty() &&
        (err = write_h265_ipb_frame(ipbs, keyframe, dts, pts)) != srs_success) {
        return srs_error_wrap(err, "write frame");
    }
    return err;
}]=])

replace_once(source [=[srs_error_t SrsGbMuxer::write_h265_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)]=]
    [=[srs_error_t SrsGbMuxer::write_h265_ipb_frame(const std::string& ipbs, bool keyframe, uint32_t dts, uint32_t pts)]=])
replace_once(source [=[    SrsHevcNaluType nt = SrsHevcNaluTypeParse(frame[0]);

    // F.3.29 intra random access point (IRAP) picture
    // ITU-T-H.265-2021.pdf, page 462.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nt >= SrsHevcNaluType_CODED_SLICE_BLA && nt <= SrsHevcNaluType_RESERVED_23) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    string ipb;
    if ((err = hevc_->mux_ipb_frame(frame, frame_size, ipb)) != srs_success){
        return srs_error_wrap(err, "hevc mux ipb frame");
    }]=] [=[    SrsVideoAvcFrameType frame_type = keyframe ? SrsVideoAvcFrameTypeKeyFrame : SrsVideoAvcFrameTypeInterFrame;]=])
replace_once(source [=[hevc_->mux_avc2flv(ipb, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)]=]
    [=[hevc_->mux_avc2flv(ipbs, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)]=])

replace_once(header [=[write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)]=]
    [=[write_h264_ipb_frame(const std::string& ibps, bool keyframe, uint32_t dts, uint32_t pts)]=])
replace_once(header [=[write_h265_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)]=]
    [=[write_h265_ipb_frame(const std::string& ipbs, bool keyframe, uint32_t dts, uint32_t pts)]=])

file(WRITE "${source_file}" "${source}")
file(WRITE "${header_file}" "${header}")
message(STATUS "Patched SRS GB28181 H.264/H.265 access-unit grouping")
