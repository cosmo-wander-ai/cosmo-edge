cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()

set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
if(NOT EXISTS "${source_file}")
    message(FATAL_ERROR "SRS GB28181 source file not found: ${source_file}")
endif()

file(READ "${source_file}" source_content)

set(setup_line "media.session_info_.setup_ = \"passive\";")
string(FIND "${source_content}" "${setup_line}" setup_position)
if(NOT setup_position EQUAL -1)
    message(STATUS "SRS GB28181 TCP setup role is already patched")
    return()
endif()

set(protocol_line "media.protos_ = \"TCP/RTP/AVP\";")
string(REGEX MATCHALL
    "media\\.protos_ = \"TCP/RTP/AVP\""
    protocol_matches
    "${source_content}")
list(LENGTH protocol_matches protocol_match_count)
if(NOT protocol_match_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one GB28181 TCP protocol assignment, found ${protocol_match_count}")
endif()

string(REPLACE
    "${protocol_line}"
    "${protocol_line}\n    ${setup_line}"
    patched_content
    "${source_content}")
file(WRITE "${source_file}" "${patched_content}")
message(STATUS "Patched SRS GB28181 SDP with passive TCP setup role")
