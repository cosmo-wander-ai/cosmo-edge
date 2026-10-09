cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()
set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
file(READ "${source_file}" source)
if(source MATCHES "COSMO_GB_QUEUE_BOUND_V1")
    return()
endif()
set(before "    // 100 videos about 30s, while 300 audios about 30s\n    bool av_overflow = nb_videos > 100 || nb_audios > 300;")
set(after [=[    // COSMO_GB_QUEUE_BOUND_V1: absent/unsupported audio must not stall video.
    // Keep the existing interleaving policy but bound its timestamp span and size.
    // Count also bounds equal/near-equal timestamps; empty queues never dereference.
    bool av_overflow = !msgs.empty() &&
        (msgs.rbegin()->first - msgs.begin()->first >= 200 || msgs.size() >= 32);]=])
string(FIND "${source}" "${before}" position)
if(position EQUAL -1)
    message(FATAL_ERROR "SRS GB queue patch anchor missing")
endif()
string(REPLACE "${before}" "${after}" source "${source}")
file(WRITE "${source_file}" "${source}")
message(STATUS "Bounded GB media interleaving to 200ms/32 messages")
