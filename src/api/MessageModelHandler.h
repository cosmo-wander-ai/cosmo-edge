#pragma once
#include <system_error>

#include "service/model/dto/ModelDto.h"

namespace cosmo::service {
class IModelService;
}  // namespace cosmo::service

namespace cosmo {

class MessageModelHandler {
public:
    explicit MessageModelHandler(service::IModelService& model_service);
    ~MessageModelHandler() = default;

    [[nodiscard]] Model::MsgPageSend Handle(Model::MsgPageRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgUploadSend Handle(Model::MsgUploadRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgListSend Handle(Model::MsgListRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgAddSend Handle(Model::MsgAddRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgUploadTempSend Handle(Model::MsgUploadTempRecv&& data,
                                                  std::error_condition& errc) const;
    [[nodiscard]] Model::MsgGetConfigSend Handle(Model::MsgGetConfigRecv&& data,
                                                 std::error_condition& errc) const;
    [[nodiscard]] Model::MsgSaveConfigSend Handle(Model::MsgSaveConfigRecv&& data,
                                                  std::error_condition& errc) const;
    [[nodiscard]] Model::MsgGetModelComponentsSend Handle(Model::MsgGetModelComponentsRecv&& data,
                                                          std::error_condition& errc) const;
    [[nodiscard]] Model::MsgDeleteSend Handle(Model::MsgDeleteRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgUpdateSend Handle(Model::MsgUpdateRecv&& data, std::error_condition& errc) const;
    [[nodiscard]] Model::MsgExportConfigSend Handle(Model::MsgExportConfigRecv&& data,
                                                    std::error_condition& errc) const;
    [[nodiscard]] Model::MsgImportModelSend Handle(Model::MsgImportModelRecv&& data,
                                                   std::error_condition& errc) const;

private:
    service::IModelService& model_service_;
};

}  // namespace cosmo
