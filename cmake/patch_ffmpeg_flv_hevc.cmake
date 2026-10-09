cmake_minimum_required(VERSION 3.16)
# Retained for reproducing the prebuilt dependency; not invoked by project builds.
if(NOT DEFINED FFMPEG_SOURCE_DIR)
    message(FATAL_ERROR "FFMPEG_SOURCE_DIR is required (a private FFmpeg 4.4.6 build copy)")
endif()

# Backport legacy FLV CodecID 12 demuxing, as used by SRS GB28181 remuxing.
# This is NOT Enhanced RTMP hvc1 support. Keep the pinned FFmpeg ABI and reuse
# its existing hvcC/parser/decoder and AVC packet framing implementation.
# Upstream reference: FFmpeg libavformat/flv{.h,dec.c}, FLV_CODECID_X_HEVC.
function(replace_once variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "FFmpeg HEVC patch anchor missing: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

file(READ "${FFMPEG_SOURCE_DIR}/libavformat/flv.h" header)
file(READ "${FFMPEG_SOURCE_DIR}/libavformat/flvdec.c" source)
if(source MATCHES "COSMO_FLV_HEVC_V1")
    return()
endif()
replace_once(header "    FLV_CODECID_H264    = 7,"
    "    FLV_CODECID_H264    = 7,\n    FLV_CODECID_X_HEVC  = 12, // Nonstandard legacy HEVC extension")
replace_once(source
    "    case FLV_CODECID_H264:\n        return vpar->codec_id == AV_CODEC_ID_H264;"
    "    case FLV_CODECID_X_HEVC:\n        return vpar->codec_id == AV_CODEC_ID_HEVC;\n    case FLV_CODECID_H264:\n        return vpar->codec_id == AV_CODEC_ID_H264;")
replace_once(source
    "    case FLV_CODECID_H264:\n        par->codec_id = AV_CODEC_ID_H264;"
    "    // COSMO_FLV_HEVC_V1: legacy HEVC has the same packet type/CTS framing.\n    case FLV_CODECID_X_HEVC:\n        par->codec_id = AV_CODEC_ID_HEVC;\n        vstream->need_parsing = AVSTREAM_PARSE_HEADERS;\n        ret = 3;\n        break;\n    case FLV_CODECID_H264:\n        par->codec_id = AV_CODEC_ID_H264;")
# All three read_packet checks must include HEVC: packet-type byte, signed CTS,
# and repeated sequence headers (queued AV_PKT_DATA_NEW_EXTRADATA).
string(REGEX MATCHALL "st->codecpar->codec_id == AV_CODEC_ID_H264" matches "${source}")
list(LENGTH matches count)
if(NOT count EQUAL 3)
    message(FATAL_ERROR "Expected three AVC packet framing checks, found ${count}")
endif()
replace_once(source "st->codecpar->codec_id == AV_CODEC_ID_H264"
    "(st->codecpar->codec_id == AV_CODEC_ID_H264 || st->codecpar->codec_id == AV_CODEC_ID_HEVC)")
file(WRITE "${FFMPEG_SOURCE_DIR}/libavformat/flv.h" "${header}")
file(WRITE "${FFMPEG_SOURCE_DIR}/libavformat/flvdec.c" "${source}")
