cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()

set(gb_source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
set(gb_header_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.hpp")
set(client_source_file "${SRS_SOURCE_DIR}/src/protocol/srs_protocol_rtmp_conn.cpp")
set(client_header_file "${SRS_SOURCE_DIR}/src/protocol/srs_protocol_rtmp_conn.hpp")
set(protocol_source_file "${SRS_SOURCE_DIR}/src/protocol/srs_protocol_rtmp_stack.cpp")
set(protocol_header_file "${SRS_SOURCE_DIR}/src/protocol/srs_protocol_rtmp_stack.hpp")
set(parts gb_source gb_header client_source client_header protocol_source protocol_header)
set(v1_parts 0)
set(v2_parts 0)
foreach(part IN LISTS parts)
    file(READ "${${part}_file}" ${part})
    if(${part} MATCHES "COSMO_GB_RTMP_CONTROL_V1")
        math(EXPR v1_parts "${v1_parts} + 1")
    endif()
    if(${part} MATCHES "COSMO_GB_RTMP_CONTROL_V2")
        math(EXPR v2_parts "${v2_parts} + 1")
    endif()
    string(REGEX MATCHALL "COSMO_GB_RTMP_CONTROL_V[0-9]+" versions "${${part}}")
    foreach(version IN LISTS versions)
        if(NOT version STREQUAL "COSMO_GB_RTMP_CONTROL_V1" AND
                NOT version STREQUAL "COSMO_GB_RTMP_CONTROL_V2")
            message(FATAL_ERROR "Unknown SRS GB RTMP control patch version: ${version}")
        endif()
    endforeach()
endforeach()
if(v2_parts EQUAL 6 AND v1_parts EQUAL 0)
    return()
elseif(v1_parts EQUAL 6 AND v2_parts EQUAL 0)
    set(upgrade_from_v1 TRUE)
elseif(v1_parts EQUAL 0 AND v2_parts EQUAL 0)
    set(upgrade_from_v1 FALSE)
else()
    message(FATAL_ERROR "Incomplete SRS GB RTMP control patch; recreate the build source copy")
endif()

function(control_replace_once variable before after)
    # Preserve whitespace-sensitive vendor anchors without trailing script spaces.
    string(REPLACE "@SRS_BLANK_4@" "    " before "${before}")
    string(REPLACE "@SRS_BLANK_8@" "        " before "${before}")
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "SRS GB RTMP control patch anchor missing: ${before}")
    endif()
    string(LENGTH "${before}" before_length)
    math(EXPR remainder_start "${position} + ${before_length}")
    string(SUBSTRING "${${variable}}" ${remainder_start} -1 remainder)
    string(FIND "${remainder}" "${before}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "SRS GB RTMP control patch anchor is ambiguous: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

if(NOT upgrade_from_v1)
# Reuse SrsProtocol's manual response queue so a receive coroutine never writes
# into an RTMP chunk while the publishing coroutine is suspended in writev.
control_replace_once(protocol_header [=[    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual int64_t get_recv_bytes();]=] [=[    // COSMO_GB_RTMP_CONTROL_V1: serialize responses with the publishing writer.
    virtual void set_auto_response(bool v);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual int64_t get_recv_bytes();]=])
control_replace_once(protocol_source [=[void SrsRtmpClient::set_recv_timeout(srs_utime_t tm)]=] [=[// COSMO_GB_RTMP_CONTROL_V1
void SrsRtmpClient::set_auto_response(bool v)
{
    protocol->set_auto_response(v);
}

void SrsRtmpClient::set_recv_timeout(srs_utime_t tm)]=])
control_replace_once(client_header [=[    virtual void set_recv_timeout(srs_utime_t timeout);]=] [=[    // COSMO_GB_RTMP_CONTROL_V1
    virtual void set_auto_response(bool v);
    virtual void set_recv_timeout(srs_utime_t timeout);]=])
control_replace_once(client_source [=[void SrsBasicRtmpClient::set_recv_timeout(srs_utime_t timeout)]=] [=[// COSMO_GB_RTMP_CONTROL_V1
void SrsBasicRtmpClient::set_auto_response(bool v)
{
    client->set_auto_response(v);
}

void SrsBasicRtmpClient::set_recv_timeout(srs_utime_t timeout)]=])

# Sending can yield to the receiver, which may append another response and
# reallocate the vector. Never keep an iterator across that yield.
control_replace_once(protocol_source [=[    std::vector<SrsPacket*>::iterator it;
    for (it = manual_response_queue.begin(); it != manual_response_queue.end();) {
        SrsPacket* pkt = *it;
@SRS_BLANK_8@
        // erase this packet, the send api always free it.
        it = manual_response_queue.erase(it);
@SRS_BLANK_8@
        // use underlayer api to send, donot flush again.]=] [=[    while (!manual_response_queue.empty()) {
        SrsPacket* pkt = manual_response_queue.front();

        // Remove before the send, which may yield and grow the response queue.
        manual_response_queue.erase(manual_response_queue.begin());

        // use underlayer api to send, donot flush again.]=])

control_replace_once(gb_header [=[class SrsGbMuxer
{]=] [=[class SrsGbMuxer : public ISrsCoroutineHandler
{]=])
control_replace_once(gb_header [=[    SrsSimpleRtmpClient* sdk_;]=] [=[    SrsSimpleRtmpClient* sdk_;
    // COSMO_GB_RTMP_CONTROL_V1: drain server controls throughout publication.
    SrsCoroutine* rtmp_receiver_;]=])
control_replace_once(gb_header [=[    srs_error_t on_ts_message(SrsTsMessage* msg);
private:]=] [=[    srs_error_t on_ts_message(SrsTsMessage* msg);
    virtual srs_error_t cycle();
private:]=])
control_replace_once(gb_source [=[    sdk_ = NULL;
    session_ = session;]=] [=[    sdk_ = NULL;
    rtmp_receiver_ = NULL;
    session_ = session;]=])

control_replace_once(gb_source [=[srs_error_t SrsGbMuxer::connect()
{]=] [=[// COSMO_GB_RTMP_CONTROL_V1: ACKs left unread can retain network pages for days.
srs_error_t SrsGbMuxer::cycle()
{
    srs_error_t err = srs_success;
    while (true) {
        if ((err = rtmp_receiver_->pull()) != srs_success) {
            return srs_error_wrap(err, "RTMP control receiver interrupted");
        }

        SrsCommonMessage* msg = NULL;
        err = sdk_->recv_message(&msg);
        srs_freep(msg);
        if (err != srs_success) {
            // The publisher observes this error through pull() and reconnects.
            // Never close here: close() must join this receiving coroutine.
            return srs_error_wrap(err, "receive RTMP control message");
        }
    }
}

srs_error_t SrsGbMuxer::connect()
{]=])
control_replace_once(gb_source [=[    // Ignore when connected.
    if (sdk_) {
        return err;
    }]=] [=[    // Reconnect after a receiver failure as well as after a send failure.
    if (sdk_) {
        if ((err = rtmp_receiver_->pull()) != srs_success) {
            close();
            return srs_error_wrap(err, "RTMP control receiver");
        }
        return err;
    }]=])
control_replace_once(gb_source [=[    if ((err = sdk_->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish");
    }

    return err;]=] [=[    if ((err = sdk_->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish");
    }

    sdk_->set_auto_response(false);
    sdk_->set_recv_timeout(SRS_UTIME_NO_TIMEOUT);
    rtmp_receiver_ = new SrsSTCoroutine("gb-rtmp-recv", this);
    if ((err = rtmp_receiver_->start()) != srs_success) {
        close();
        return srs_error_wrap(err, "start RTMP control receiver");
    }

    return err;]=])
control_replace_once(gb_source [=[void SrsGbMuxer::close()
{
    srs_freep(sdk_);]=] [=[void SrsGbMuxer::close()
{
    // Join before releasing the client used by the receive coroutine.
    if (rtmp_receiver_) rtmp_receiver_->stop();
    srs_freep(rtmp_receiver_);
    srs_freep(sdk_);]=])
control_replace_once(gb_source [=[    h264_sps_ = "";
    h264_pps_ = "";
}]=] [=[    h264_sps_ = "";
    h264_pps_ = "";
#ifdef SRS_H265
    vps_sps_pps_sent_ = false;
    vps_sps_pps_change_ = false;
    h265_vps_ = h265_sps_ = h265_pps_ = "";
#endif
}]=])
control_replace_once(gb_source [=[srs_error_t SrsGbMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }]=] [=[srs_error_t SrsGbMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "connect");
    }]=])
endif()

# Upgrade both a fresh V1-shaped source and an existing complete V1 build copy.
# The send path itself calls manual_response_flush(), so only the outermost
# invocation may consume the queue. Keep a single writer across coroutine yields.
control_replace_once(protocol_header [=[    std::vector<SrsPacket*> manual_response_queue;]=]
    [=[    std::vector<SrsPacket*> manual_response_queue;
    bool flushing_responses_;]=])
control_replace_once(protocol_source [=[    auto_response_when_recv = true;]=]
    [=[    auto_response_when_recv = true;
    flushing_responses_ = false;]=])
control_replace_once(protocol_source [=[srs_error_t SrsProtocol::manual_response_flush()
{
    srs_error_t err = srs_success;
@SRS_BLANK_4@
    if (manual_response_queue.empty()) {
        return err;
    }
@SRS_BLANK_4@
    while (!manual_response_queue.empty()) {
        SrsPacket* pkt = manual_response_queue.front();

        // Remove before the send, which may yield and grow the response queue.
        manual_response_queue.erase(manual_response_queue.begin());

        // use underlayer api to send, donot flush again.
        if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
@SRS_BLANK_4@
    return err;
}]=] [=[srs_error_t SrsProtocol::manual_response_flush()
{
    srs_error_t err = srs_success;

    if (flushing_responses_ || manual_response_queue.empty()) {
        return err;
    }

    flushing_responses_ = true;
    while (!manual_response_queue.empty()) {
        SrsPacket* pkt = manual_response_queue.front();

        // Remove before sending: a receive coroutine may append while we yield.
        manual_response_queue.erase(manual_response_queue.begin());

        // Sending reenters this function; the outer loop drains all responses.
        if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
            flushing_responses_ = false;
            return srs_error_wrap(err, "send packet");
        }
    }
    flushing_responses_ = false;

    return err;
}]=])

# A failed sequence-header send leaves both changed flags set. Clear them along
# with the cached parameters, then wait for a complete new pair in either order.
control_replace_once(gb_source [=[    // Wait for the next AVC sequence header.
    h264_sps_pps_sent_ = false;]=] [=[    // Wait for the next AVC sequence header.
    h264_sps_pps_sent_ = false;
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;]=])
control_replace_once(gb_source [=[    if (!h264_sps_changed_ || !h264_pps_changed_) {
        return err;
    }

    // h264 raw to h264 packet.]=] [=[    if (!h264_sps_changed_ || !h264_pps_changed_) {
        return err;
    }
    if (h264_sps_.length() < 4 || h264_pps_.empty()) {
        return err;
    }

    // h264 raw to h264 packet.]=])

foreach(part IN LISTS parts)
    string(REPLACE "COSMO_GB_RTMP_CONTROL_V1" "COSMO_GB_RTMP_CONTROL_V2" ${part} "${${part}}")
endforeach()

# Validate every anchor before writing any of the build-copy files.
foreach(part IN LISTS parts)
    file(WRITE "${${part}_file}" "${${part}}")
endforeach()
message(STATUS "Applied GB RTMP control V2: drain controls, serialize responses and reset reconnect state")
