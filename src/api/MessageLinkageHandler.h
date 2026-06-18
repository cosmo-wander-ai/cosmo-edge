#pragma once
#include <system_error>

#include "service/infra/dto/LinkageDto.h"

namespace cosmo::service {
class ILinkageService;
}  // namespace cosmo::service

namespace cosmo {

class MessageLinkageHandler {
public:
    explicit MessageLinkageHandler(service::ILinkageService& linkage_svc);
    ~MessageLinkageHandler() = default;

    MessageLinkageHandler(const MessageLinkageHandler&)            = delete;
    MessageLinkageHandler& operator=(const MessageLinkageHandler&) = delete;

    [[nodiscard]] Linkage::MsgStoragesSend Handle(Linkage::MsgStoragesRecv&& data,
                                                  std::error_condition& errc) const;
    [[nodiscard]] Linkage::MsgAddSend Handle(Linkage::MsgAddRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Linkage::MsgDeleteSend Handle(Linkage::MsgDeleteRecv&& data,
                                                std::error_condition& errc) const;
    [[nodiscard]] Linkage::MsgUpdateSend Handle(Linkage::MsgUpdateRecv&& data,
                                                std::error_condition& errc) const;
    [[nodiscard]] Linkage::MsgPageSend Handle(Linkage::MsgPageRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Linkage::MsgSwitchSend Handle(Linkage::MsgSwitchRecv&& data,
                                                std::error_condition& errc) const;

private:
    service::ILinkageService& linkage_svc_;
};

}  // namespace cosmo
