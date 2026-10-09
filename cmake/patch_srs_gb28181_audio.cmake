cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()
set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
set(header_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.hpp")
file(READ "${source_file}" source)
file(READ "${header_file}" header)
if(source MATCHES "COSMO_GB_AUDIO_GUARD_V1")
    return()
endif()
function(replace_audio variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "SRS GB audio patch anchor missing: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

# The PS parser leaves audio_stream_type_ reserved for unsupported formats.
# Previously every non-video PES reached the AAC/ADTS parser, including G.711
# and private NVR streams. Drop those payloads before muxing or RTMP connection.
replace_audio(source [=[srs_error_t SrsGbMuxer::on_ts_message(SrsTsMessage* msg)
{
    srs_error_t err = srs_success;
]=] [=[srs_error_t SrsGbMuxer::on_ts_message(SrsTsMessage* msg)
{
    srs_error_t err = srs_success;

    // COSMO_GB_AUDIO_GUARD_V1: only declared AAC audio belongs in the ADTS muxer.
    if (msg->sid != SrsTsPESStreamIdVideoCommon) {
        SrsPsDecodeHelper* h = (SrsPsDecodeHelper*)msg->ps_helper_;
        if (msg->sid != SrsTsPESStreamIdAudioCommon || !h || !h->ctx_ ||
            h->ctx_->audio_stream_type_ != SrsTsStreamAudioAAC) {
            return srs_success;
        }
    }
]=])

# Keep malformed AAC observable without formatting a stack and writing a warning
# for every audio packet. The time gate is per session; SRS runs these on its
# existing single coroutine thread. Always release the error, including skipped logs.
replace_audio(header "    SrsGbSessionState state_;"
    "    SrsGbSessionState state_;\n    srs_utime_t next_audio_warning_;")
replace_audio(source "    state_ = SrsGbSessionStateInit;"
    "    state_ = SrsGbSessionStateInit;\n    next_audio_warning_ = 0;")
replace_audio(source [=[            srs_warn("Muxer: Ignore audio err %s", srs_error_desc(err).c_str());
            srs_freep(err);]=] [=[            srs_utime_t now = srs_update_system_time();
            if (now >= next_audio_warning_) {
                next_audio_warning_ = now + 10 * SRS_UTIME_SECONDS;
                srs_warn("Muxer: Ignore audio err (limited to once per 10s) %s", srs_error_desc(err).c_str());
            }
            srs_freep(err);]=])
file(WRITE "${source_file}" "${source}")
file(WRITE "${header_file}" "${header}")
message(STATUS "Guarded GB AAC muxing and rate-limited malformed audio warnings")
