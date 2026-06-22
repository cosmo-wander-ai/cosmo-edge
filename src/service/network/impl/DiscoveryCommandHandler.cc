// DiscoveryCommandHandler.cc — Multicast command handlers for device discovery.
// Split from DeviceDiscoveryServiceImpl.cc to reduce file size (DEBT-R03).

#include <cstring>
#include <nlohmann/json.hpp>
#include <thread>

#include "network/http/HttpRequest.h"
#include "service/detail/ServiceRegistry.h"
#include "service/network/INetworkService.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"
#include "service/system/IDeviceInfoService.h"
#include "util/JsonFileUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/TimingConstants.h"
#include "util/Version.h"

namespace cosmo::service {

constexpr int k_dev_info_length = 1024;

// HW info on-disk structure (legacy binary format).
struct MvDevInfo {
    char dev_type[32];
    char hw_version[32];
    char serial_num[32];
    char reserved[928];
};

// ============================================================
//  Probe handler
// ============================================================

void DeviceDiscoveryServiceImpl::HandleProbe(DiscoveryProbeRecv&& data) {
    DiscoveryProbeSend retData{};
    retData.cmd   = data.cmd;
    retData.type  = "ack";
    retData.reqId = data.reqId;

    std::error_condition errc{};
    auto cards = ServiceRegistry::Instance().Get<INetworkService>().GetCardRealInfos();
    for (auto& card : cards) {
        MsgNetCardInfo info;
        info.mainCard = card.is_main;
        info.dhcp     = card.dhcp;
        info.ethName  = card.eth_name;
        info.ipAddr   = card.ip_addr;
        info.netMask  = card.net_mask;
        info.gateway  = card.gateway;
        retData.resData.netCardList.push_back(info);
    }

    if (retData.resData.netCardList.empty()) {
        LOG_ERRO("{}", "get netCardList failed");
        errc = util::ErrorEnum::Failed;
    } else {
        //    LicenceManager lic;
        retData.resData.devInfoList.push_back(
            {"deviceType", "设备型号", ServiceRegistry::Instance().Get<IDeviceInfoService>().GetDevModel()});
        retData.resData.devInfoList.push_back({"softwareVersion", "软件版本", util::GetAbbrVersion()});
        retData.resData.devInfoList.push_back(
            {"deviceSn", "设备SN", ServiceRegistry::Instance().Get<IDeviceInfoService>().GetDevSn()});
    }

    retData.resCode = errc.value();
    retData.resMsg  = errc.message();
    SendMessage(std::move(nlohmann::json(retData).dump()), data.from);
}

// ============================================================
//  Message dispatch
// ============================================================

void DeviceDiscoveryServiceImpl::HandleRecvMsg(InternalMsg&& data) {
    if (data.vague.cmd == "modifyNetCard") {
        HandleModifyNetCard(std::move(data));
        return;
    }
    if (data.vague.cmd == "writeHWInfo") {
        HandleWriteHWInfo(std::move(data));
        return;
    }
    if (data.vague.cmd == "modifyAuthCode") {
        HandleModifyAuthCode(std::move(data));
        return;
    }
    if (data.vague.cmd == "queryAuthMessage") {
        HandleQueryAuthMessage(std::move(data));
        return;
    }
}

// ============================================================
//  Auth query handler
// ============================================================

void DeviceDiscoveryServiceImpl::HandleQueryAuthMessage(InternalMsg&& data) {
    AuthStatusQueryResponse retData{};
    retData.cmd   = data.vague.cmd;
    retData.type  = "ack";
    retData.reqId = data.vague.reqId;

    AuthStatusQueryRequest recv_data{};

    std::error_condition errc = util::ErrorEnum::Success;
    std::string result        = "成功";
    do {
        try {
            {
                auto _j = nlohmann::json::parse(data.raw_json);
                _j.get_to(recv_data);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", data.raw_json, e.what());
            errc           = util::ErrorEnum::ParameterException;
            retData.resMsg = "消息解析错误";
            break;
        }
        std::string str_token;
        bool b_ret = GetToken(str_token, errc, retData.resMsg);
        if (!b_ret) {
            break;
        }

        // Request authorization
        network::http::HttpStringHandler httpStrHandler2;
        std::string str_url = "http://127.0.0.1/interface/queryAuthorMessage";
        network::http::HttpRequest httpReq(str_url, httpStrHandler2);
        AuthQueryHttpRequest sendData{};
        httpReq.AppendHeader("Token", str_token);
        httpReq.AppendHeader("Expect", "");
        httpReq.SetContentType("application/json");
        httpReq.SetData(nlohmann::json(sendData).dump().c_str());
        httpReq.SetTimeout(1);

        auto ret_status = httpReq.Submit();
        if (ret_status != 200) {
            LOG_ERRO("http send data to {} error, status {}", str_url.c_str(), ret_status);
            errc           = util::ErrorEnum::AuthAlgorithmFailed;
            retData.resMsg = "授权失败";
            break;
        }
        AuthQueryHttpResponse authRecvData{};
        auto ret_json = httpStrHandler2.GetData();
        try {
            {
                auto _j = nlohmann::json::parse(ret_json);
                _j.get_to(authRecvData);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", ret_json, e.what());
            errc           = util::ErrorEnum::ParameterException;
            retData.resMsg = "响应消息解析错误";
            break;
        }
        retData.resData.serverList = authRecvData.resData.serverList;
    } while (false);
    retData.resCode = errc.value();
    if (retData.resMsg.empty()) {
        retData.resMsg  = result;
        retData.resCode = 0;
    }
    SendMessage(std::move(nlohmann::json(retData).dump()), data.from);
}

// ============================================================
//  HW info write handler
// ============================================================

void DeviceDiscoveryServiceImpl::HandleWriteHWInfo(InternalMsg&& data) {
    HWInfoWriteResponse retData;
    retData.cmd   = data.vague.cmd;
    retData.type  = "ack";
    retData.reqId = data.vague.reqId;

    HWInfoWriteRequest recv_data{};
    std::error_condition errc = util::ErrorEnum::Success;
    std::string result        = "成功";
    do {
        try {
            {
                auto _j = nlohmann::json::parse(data.raw_json);
                _j.get_to(recv_data);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", data.raw_json, e.what());
            errc           = util::ErrorEnum::ParameterException;
            retData.resMsg = "消息解析错误";
            break;
        }
        if (!WriteHWInfo(recv_data.devHWInfo)) {
            errc           = util::ErrorEnum::FileOpenFailed;
            retData.resMsg = "硬件信息录入失败";
        }
    } while (false);
    retData.resCode = errc.value();
    if (retData.resMsg.empty()) {
        retData.resMsg  = result;
        retData.resCode = 0;
    }
    if (!retData.resCode) {
        retData.devHWInfo = recv_data.devHWInfo;
    }
    SendMessage(std::move(nlohmann::json(retData).dump()), data.from);
}

bool DeviceDiscoveryServiceImpl::WriteHWInfo(const DeviceHWInfo& info) {
    std::string str_file_path = "/root/.mvdevcfg";
    if (info.devType == "MV-BH1L04T") {
        if (strncmp(info.devSn.c_str(), "MV", 2)) {
            LOG_ERRO("MV-BH1L04T, illegal SN[{}]", info.devSn.c_str());
            return false;
        }
    } else if (info.devType == "MV-BS1L04T") {
        str_file_path = "/app/userdata/.mvdevcfg";
        if (strncmp(info.devSn.c_str(), "SL", 2)) {
            LOG_ERRO("MV-BS1L04T, illegal SN[{}]", info.devSn.c_str());
            return false;
        }
    } else {
        LOG_ERRO("Wrong Type:{}", info.devType);
        return false;
    }

    FILE* fp = fopen(str_file_path.c_str(), "w");
    if (!fp) {
        LOG_ERRO("{}", "Open file fail.");
        return false;
    }
    char readBuf[1536] = {0};
    auto* pDevInfo     = reinterpret_cast<MvDevInfo*>(readBuf);
    strncpy(pDevInfo->dev_type, info.devType.c_str(), 32);
    strncpy(pDevInfo->hw_version, info.hwVersion.c_str(), 32);
    strncpy(pDevInfo->serial_num, info.devSn.c_str(), 32);
    size_t n_length = fwrite(pDevInfo, k_dev_info_length, 1, fp);
    if (n_length == 0) {
        LOG_ERRO("{}", "Write file fail.");
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

// ============================================================
//  Token acquisition helper
// ============================================================

bool DeviceDiscoveryServiceImpl::GetToken(std::string& str_token, std::error_condition& errc,
                                          std::string& strResult) {
    bool b_ret = false;
    do {
        // Get token
        network::http::HttpStringHandler httpStrHandler;
        std::string str_token_url = "http://127.0.0.1/interface/login";
        network::http::HttpRequest tokenReq(str_token_url, httpStrHandler);
        LoginHttpRequest tokenSendData{};
        tokenSendData.language = 0;
        tokenSendData.user     = "admin";
        tokenSendData.passwd   = "21232F297A57A5A743894A0E4A801FC3";  // MD5 of "admin"
        tokenReq.AppendHeader("Expect", "");
        tokenReq.SetContentType("application/json");
        tokenReq.SetData(nlohmann::json(tokenSendData).dump().c_str());
        tokenReq.SetTimeout(1);
        auto ret_status = tokenReq.Submit();
        if (ret_status != 200) {
            LOG_ERRO("http send data to {} error, status {}", str_token_url.c_str(), ret_status);
            errc      = util::ErrorEnum::AuthAlgorithmFailed;
            strResult = "获取token失败";
            break;
        }

        auto ret_json = httpStrHandler.GetData();
        LoginHttpResponse tokenRecvData{};
        try {
            {
                auto _j = nlohmann::json::parse(ret_json);
                _j.get_to(tokenRecvData);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", ret_json, e.what());
            errc      = util::ErrorEnum::ParameterException;
            strResult = "响应消息解析错误";
            break;
        }
        str_token = tokenRecvData.resData.token;
        if (str_token.empty()) {
            errc      = util::ErrorEnum::AuthAlgorithmFailed;
            strResult = "Token获取失败";
            break;
        }
        errc      = util::ErrorEnum::Success;
        strResult = "成功";
        b_ret     = true;
    } while (false);
    return b_ret;
}

// ============================================================
//  Auth code modification handler
// ============================================================

void DeviceDiscoveryServiceImpl::HandleModifyAuthCode(InternalMsg&& data) {
    AuthCodeModifyResponse retData{};
    retData.cmd   = data.vague.cmd;
    retData.type  = "ack";
    retData.reqId = data.vague.reqId;

    AuthCodeModifyRequest recv_data{};

    std::error_condition errc = util::ErrorEnum::Success;
    std::string result        = "成功";
    do {
        try {
            {
                auto _j = nlohmann::json::parse(data.raw_json);
                _j.get_to(recv_data);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", data.raw_json, e.what());
            errc           = util::ErrorEnum::ParameterException;
            retData.resMsg = "消息解析错误";
            break;
        }
        std::string str_token;
        bool b_ret = GetToken(str_token, errc, retData.resMsg);
        if (!b_ret) {
            break;
        }

        // Request authorization
        std::string str_url = "http://127.0.0.1/interface/modifyAuthorCode";
        network::http::HttpRequest httpReq(str_url);
        AuthCodeHttpRequest sendData{};
        sendData.authorCode = recv_data.authCode;
        httpReq.AppendHeader("Token", str_token);
        httpReq.AppendHeader("Expect", "");
        httpReq.SetContentType("application/json");
        httpReq.SetData(nlohmann::json(sendData).dump().c_str());
        httpReq.SetTimeout(1);

        auto ret_status = httpReq.Submit();
        if (ret_status != 200) {
            LOG_ERRO("http send data to {} error, status {}, authCode: {}", str_url.c_str(), ret_status,
                     recv_data.authCode.c_str());
            errc           = util::ErrorEnum::AuthAlgorithmFailed;
            retData.resMsg = "授权失败";
            break;
        }

        // Request authorization
        network::http::HttpStringHandler httpStrHandler2;
        std::string str_query_url = "http://127.0.0.1/interface/queryAuthorMessage";
        network::http::HttpRequest httpQueryReq(str_query_url, httpStrHandler2);
        AuthQueryHttpRequest sendQueryData{};
        httpQueryReq.AppendHeader("Token", str_token);
        httpQueryReq.AppendHeader("Expect", "");
        httpQueryReq.SetContentType("application/json");
        httpQueryReq.SetData(nlohmann::json(sendQueryData).dump().c_str());
        httpQueryReq.SetTimeout(1);

        ret_status = httpQueryReq.Submit();
        if (ret_status != 200) {
            LOG_ERRO("http send data to {} error, status {}", str_query_url.c_str(), ret_status);
            errc           = util::ErrorEnum::AuthAlgorithmFailed;
            retData.resMsg = "授权失败";
            break;
        }
        AuthQueryHttpResponse authRecvData{};
        auto ret_json = httpStrHandler2.GetData();
        try {
            {
                auto _j = nlohmann::json::parse(ret_json);
                _j.get_to(authRecvData);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", ret_json, e.what());
            errc           = util::ErrorEnum::ParameterException;
            retData.resMsg = "响应消息解析错误";
            break;
        }
        LOG_ERRO("ret_json:{}", ret_json.c_str());
        retData.resData.serverList = authRecvData.resData.serverList;
    } while (false);
    retData.resCode = errc.value();
    if (retData.resMsg.empty()) {
        retData.resMsg  = result;
        retData.resCode = 0;
    }
    SendMessage(std::move(nlohmann::json(retData).dump()), data.from);
}

// ============================================================
//  NetCard modification handler
// ============================================================

void DeviceDiscoveryServiceImpl::HandleModifyNetCard(InternalMsg&& data) {
    ModifyNetCardResponse retData{};
    retData.cmd   = data.vague.cmd;
    retData.type  = "ack";
    retData.reqId = data.vague.reqId;

    ModifyNetCardRequest recv_data{};
    std::error_condition errc = util::ErrorEnum::Success;
    std::string result;
    do {
        try {
            {
                auto _j = nlohmann::json::parse(data.raw_json);
                _j.get_to(recv_data);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, catch {}", data.raw_json, e.what());
            errc = util::ErrorEnum::ParameterException;
            break;
        }

        PasswordFile passwd;
        try {
            nlohmann::json doc;
            auto load_ret = util::JsonFileUtil::ReadJsonFile(cosmo::path::GetCfgPath() + "/auth.json", doc);
            if (load_ret == util::ErrorEnum::Success) {
                passwd = doc.get<PasswordFile>();
            }
        } catch (const std::exception& e) {
            LOG_ERRO("read passwd failed {}", e.what());
        }

        if (util::ToUpper(recv_data.passwd) != passwd.userPasswd.admin) {
            errc           = util::ErrorEnum::AuthFailed;
            retData.resMsg = "密码错误";
            break;
        }

        //    IPInfo info{};
        std::vector<std::string> DNS{recv_data.netCard.dns1, recv_data.netCard.dns2};

        platform::NetCardInfo info;

        info.is_main  = true;
        info.dhcp     = recv_data.netCard.dhcp;
        info.eth_name = recv_data.netCard.ethName;
        info.ip_addr  = recv_data.netCard.ipAddr;
        info.net_mask = recv_data.netCard.netMask;
        info.gateway  = recv_data.netCard.gateway;
        if (ServiceRegistry::Instance().Get<INetworkService>().SearchSetNewInfo(info, recv_data.netCard.dns1,
                                                                                recv_data.netCard.dns2)) {
            // Join any previous netcard config thread before starting a new one
            if (netcard_thread_.joinable()) {
                netcard_thread_.join();
            }
            netcard_thread_ = std::thread([info, DNS]() {
                std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
                platform::DoNetCard(info);
                platform::DnsEffect(DNS);
            });
        }
    } while (false);

    retData.resCode = errc.value();
    if (retData.resMsg.empty()) {
        retData.resMsg  = result;
        retData.resCode = 0;
    }
    SendMessage(std::move(nlohmann::json(retData).dump()), data.from);
}

}  // namespace cosmo::service
