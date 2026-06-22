#pragma once
#include <system_error>

#include "service/network/dto/AuthDto.h"

namespace cosmo::service {
class IAuthService;
}  // namespace cosmo::service

namespace cosmo {

class MessageAuthHandler {
public:
    explicit MessageAuthHandler(service::IAuthService& auth_svc);

    [[nodiscard]] Auth::MsgDoLoginSend Handle(Auth::MsgDoLoginRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Auth::MsgModifyPasswordSend Handle(Auth::MsgModifyPasswordRecv&& data,
                                                     std::error_condition& errc) const;

private:
    service::IAuthService& auth_svc_;
};

}  // namespace cosmo
