cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()
set(source_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.cpp")
set(header_file "${SRS_SOURCE_DIR}/src/app/srs_app_gb28181.hpp")
file(READ "${source_file}" source)
file(READ "${header_file}" header)
if(source MATCHES "COSMO_MANAGED_GB_V1")
    return()
endif()
function(replace_once variable before after)
    string(FIND "${${variable}}" "${before}" location)
    if(location EQUAL -1)
        message(FATAL_ERROR "Managed GB patch anchor missing: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()
replace_once(header "    SrsGbSessionState state_;" "    SrsGbSessionState state_;\n    bool external_;\n    srs_utime_t external_deadline_;")
replace_once(header "    SrsGbSession();" "    void stop_external();\n    SrsGbSession();")
replace_once(source "    state_ = SrsGbSessionStateInit;" "    state_ = SrsGbSessionStateInit;\n    external_ = false;\n    external_deadline_ = 0;")
replace_once(source "    pip_ = candidate_ = _srs_config->get_stream_caster_sip_candidate(conf);" "    external_ = !_srs_config->get_stream_caster_sip_enable(conf);\n    external_deadline_ = srs_update_system_time() + 60 * SRS_UTIME_SECONDS;\n    pip_ = candidate_ = _srs_config->get_stream_caster_sip_candidate(conf);")
replace_once(source "    // Got a new context, that is new media transport." "    external_deadline_ = srs_update_system_time() + 20 * SRS_UTIME_SECONDS;\n    // Got a new context, that is new media transport.")
replace_once(source "        // Client send bye, we should dispose the session." "        if (external_ && srs_update_system_time() >= external_deadline_) return srs_error_new(ERROR_GB_TIMEOUT, \"external media lease expired\");\n        // Client send bye, we should dispose the session.")
replace_once(source "    receiver_->interrupt();\n    sender_->interrupt();" "    if (receiver_) receiver_->interrupt();\n    if (sender_) sender_->interrupt();")
replace_once(source "void SrsGbSession::on_executor_done(ISrsInterruptable* executor)" "void SrsGbSession::stop_external()\n{\n    if (external_ && owner_coroutine_) owner_coroutine_->interrupt();\n}\n\nvoid SrsGbSession::on_executor_done(ISrsInterruptable* executor)")
replace_once(source "    // For each GB session, we use short-term HTTP connection." [=[    // COSMO_MANAGED_GB_V1: allocation/release are control-plane operations, not public RTC APIs.
    SrsHttpMessage* message = dynamic_cast<SrsHttpMessage*>(r);
    if (!message || !message->connection() || message->connection()->remote_ip() != "127.0.0.1" || !r->is_http_post()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "local POST required");
    }
    // For each GB session, we use short-term HTTP connection.]=])
replace_once(source "    // Fetch params from req object." [=[    if (_srs_config->get_stream_caster_sip_enable(conf_)) return srs_error_new(ERROR_HTTP_DATA_INVALID, "external SIP required");
    res->set("managed", SrsJsonAny::boolean(true));
    SrsJsonAny* action = req->ensure_property_string("action");
    if (action && action->to_str() == "status") {
        res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
        res->set("port", SrsJsonAny::integer(_srs_config->get_stream_caster_listen(conf_)));
        res->set("is_tcp", SrsJsonAny::boolean(true));
        return err;
    }
    // Fetch params from req object.]=])
replace_once(source "    uint64_t ssrc = atoi(prop->to_str().c_str());" [=[    string number = prop->to_str();
    if (id.size() != 20 || id.find_first_not_of("0123456789") != string::npos || number.empty() || number.size() > 10 || number.find_first_not_of("0123456789") != string::npos) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid identity");
    }
    uint64_t ssrc = strtoull(number.c_str(), NULL, 10);
    if (!ssrc || ssrc > 0xffffffffULL) return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid SSRC");
    if (action && action->to_str() == "release") {
        SrsSharedResource<SrsGbSession>* session = dynamic_cast<SrsSharedResource<SrsGbSession>*>(_srs_gb_manager->find_by_id(id));
        if (session && _srs_gb_manager->find_by_fast_id(ssrc) == session) (*session)->stop_external();
        res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
        return err;
    }
    if (action && action->to_str() != "allocate") return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid action");]=])
file(WRITE "${source_file}" "${source}")
file(WRITE "${header_file}" "${header}")
message(STATUS "Patched SRS with local managed GB media leases and safe external-session cleanup")
