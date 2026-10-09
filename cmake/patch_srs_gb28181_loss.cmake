cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()
set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
set(header_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.hpp")
file(READ "${source_file}" source)
file(READ "${header_file}" header)
if(source MATCHES "COSMO_GB_RTP_LOSS_V1")
    return()
endif()
function(replace_once variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "GB RTP loss patch anchor missing")
    endif()
    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()
replace_once(header "    int recover_;" "    int recover_;\n    bool rtp_seen_;\n    uint16_t rtp_next_;")
replace_once(source "SrsRecoverablePsContext::SrsRecoverablePsContext()\n{\n    recover_ = 0;"
    "SrsRecoverablePsContext::SrsRecoverablePsContext()\n{\n    recover_ = 0;\n    rtp_seen_ = false;\n    rtp_next_ = 0;")
replace_once(source "    // If got reserved bytes, move to the start of payload." [=[    // COSMO_GB_RTP_LOSS_V1: the managed UDP bridge preserves RTP sequence gaps.
    // Never stitch a damaged PES or reserved fragment onto the next RTP payload.
    uint16_t sequence = rtp.header.get_sequence();
    if (rtp_seen_ && sequence != rtp_next_) {
        reserved = 0;
        SrsTsMessage* incomplete = ctx_.reap(); srs_freep(incomplete);
        recover_ = 1;
        handler->on_recover_mode(recover_);
    }
    rtp_seen_ = true;
    rtp_next_ = uint16_t(sequence + 1);

    // If got reserved bytes, move to the start of payload.]=])
file(WRITE "${source_file}" "${source}")
file(WRITE "${header_file}" "${header}")
message(STATUS "Patched GB RTP gaps to discard partial PES and resynchronize PS")
