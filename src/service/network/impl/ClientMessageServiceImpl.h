// ClientMessageService implementation — handles platform HTTP API calls

#pragma once

#include "service/network/IClientMessageService.h"

namespace cosmo::service {

class ClientMessageServiceImpl : public IClientMessageService {
public:
    ClientMessageServiceImpl() = default;

    bool FetchAlgorithmConfig(cosmo::CMsgAlgorithmProcessConfigNGReq& req,
                              cosmo::CMsgAlgorithmProcessConfigNGRsp& rsp) override;
    bool FetchAtomicCodeList(cosmo::CMsgGetAtomicCodeListReq& req,
                             cosmo::CMsgGetAtomicCodeListRsp& rsp) override;
    bool FetchVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& req, cosmo::CMsgGetVideoPlayRsp& rsp) override;
    void NodeOperatorEventPush(cosmo::MsgOperateNodeRecv& data) override;
};

}  // namespace cosmo::service
