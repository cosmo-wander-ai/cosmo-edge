// MessageLinkageHandler — Message Linkage Handler implementation.

#include "api/MessageLinkageHandler.h"

#include "service/infra/ILinkageService.h"

namespace cosmo {
MessageLinkageHandler::MessageLinkageHandler(service::ILinkageService& linkage_svc)
    : linkage_svc_(linkage_svc) {}

// Add
Linkage::MsgAddSend MessageLinkageHandler::Handle(Linkage::MsgAddRecv&& data,
                                                  std::error_condition& errc) const {
    Linkage::MsgAddSend retData{};

    errc = linkage_svc_.Add(data.name, data.workFlow, retData.resData.id);
    return retData;
}

Linkage::MsgDeleteSend MessageLinkageHandler::Handle(Linkage::MsgDeleteRecv&& data,
                                                     std::error_condition& errc) const {
    Linkage::MsgDeleteSend retData{};

    errc = linkage_svc_.Delete(data.id);
    return retData;
}

Linkage::MsgUpdateSend MessageLinkageHandler::Handle(Linkage::MsgUpdateRecv&& data,
                                                     std::error_condition& errc) const {
    Linkage::MsgUpdateSend retData{};

    errc = linkage_svc_.Update(data.name, data.id, data.workFlow);
    return retData;
}

Linkage::MsgPageSend MessageLinkageHandler::Handle(Linkage::MsgPageRecv&& data,
                                                   std::error_condition& /*errc*/) const {
    Linkage::MsgPageSend retData{};

    retData.resData.tasks = linkage_svc_.Query(data.pageNum, data.pageSize, data.name, retData.resData.total);
    return retData;
}

Linkage::MsgStoragesSend MessageLinkageHandler::Handle(Linkage::MsgStoragesRecv&& /*data*/,
                                                       std::error_condition& /*errc*/) const {
    Linkage::MsgStoragesSend retData{};
    int total_size = 0;
    linkage_svc_.ReadSupportedStorage(total_size, retData.resData.storages);
    return retData;
}

Linkage::MsgSwitchSend MessageLinkageHandler::Handle(Linkage::MsgSwitchRecv&& data,
                                                     std::error_condition& errc) const {
    Linkage::MsgSwitchSend retData{};

    errc = linkage_svc_.Switch(data.id, data.enable);
    return retData;
}
}  // namespace cosmo