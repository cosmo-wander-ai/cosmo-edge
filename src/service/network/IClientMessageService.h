/// @file IClientMessageService.h
/// @brief Client message service interface — platform HTTP API abstraction
///        for algorithm config, atomic code lists, and video playback URLs.
#pragma once

#include "service/detail/ServiceRegistry.h"
#include "service/network/dto/ClientMsgAlgorithm.h"
#include "service/network/dto/ClientMsgVideo.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::service {

/// Abstracts outgoing HTTP API calls to the platform server.
///
/// Used by service/flow layers to fetch algorithm configurations, atomic
/// code lists, and video playback URLs from the remote management platform.
class IClientMessageService {
public:
    virtual ~IClientMessageService() = default;

    /// Fetch algorithm orchestration configuration from the platform.
    /// @param req Request payload.
    /// @param rsp [out] Response with algorithm config.
    /// @return true on success.
    virtual bool FetchAlgorithmConfig(cosmo::CMsgAlgorithmProcessConfigNGReq& req,
                                      cosmo::CMsgAlgorithmProcessConfigNGRsp& rsp) = 0;

    /// Fetch the list of available atomic algorithm codes from the platform.
    /// @param req Request payload.
    /// @param rsp [out] Response with atomic code list.
    /// @return true on success.
    virtual bool FetchAtomicCodeList(cosmo::CMsgGetAtomicCodeListReq& req,
                                     cosmo::CMsgGetAtomicCodeListRsp& rsp) = 0;

    /// Fetch a video playback URL from the platform.
    /// @param req Request payload with channel/stream info.
    /// @param rsp [out] Response with playback URL.
    /// @return true on success.
    virtual bool FetchVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& req, cosmo::CMsgGetVideoPlayRsp& rsp) = 0;

    /// Push a node operation event to the platform.
    /// @param data Node operation event data.
    virtual void NodeOperatorEventPush(cosmo::MsgOperateNodeRecv& data) = 0;
};

}  // namespace cosmo::service
