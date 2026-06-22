// ClientMessageServiceImpl — ClientMessageService implementation — handles platform HTTP API calls

#include "service/network/impl/ClientMessageServiceImpl.h"

#include "util/Log.h"

namespace cosmo::service {

bool ClientMessageServiceImpl::FetchAlgorithmConfig(cosmo::CMsgAlgorithmProcessConfigNGReq& /*req*/,
                                                    cosmo::CMsgAlgorithmProcessConfigNGRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:algorithmProcessConfigNG But No Cb");
    return false;
}

bool ClientMessageServiceImpl::FetchAtomicCodeList(cosmo::CMsgGetAtomicCodeListReq& /*req*/,
                                                   cosmo::CMsgGetAtomicCodeListRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:getAtomicCodeList But No Cb");
    return false;
}

bool ClientMessageServiceImpl::FetchVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& /*req*/,
                                                 cosmo::CMsgGetVideoPlayRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:getVideoPlay But No Cb");
    return false;
}

void ClientMessageServiceImpl::NodeOperatorEventPush(cosmo::MsgOperateNodeRecv& data) {
    LOG_WARN("{} Have Not Push. [PUSH QUEUE HAVE NOT REGIST]", data.operateType);
}

}  // namespace cosmo::service
