#pragma once

#include <string>

#include "nn/core/macros.h"

namespace cosmo::nn {

enum StatusCode {
    COSMO_NN_OK = 0x0,

    // param error
    COSMO_NN_ERR_PARAM = 0x1000,
    COSMO_NN_ERR_NULL_PARAM,
    COSMO_NN_ERR_INVALID_CFG,

    // network error
    COSMO_NN_ERR_NET = 0x2000,
    COSMO_NN_ERR_UNSUPPORT_NET,
    COSMO_NN_ERR_NODE_LOAD_PARAM,
    COSMO_NN_ERR_NODE_CREATE,
    COSMO_NN_ERR_NODE_FORWARD,

    // common err
    COSMO_NN_ERR_INVALID_INPUT = 0x3000,
    COSMO_NN_ERR_OPEN_FILE,
    COSMO_NN_ERR_OUT_OF_MEMORY,
    COSMO_NN_ERR_LOAD_MODEL,
    COSMO_NN_ERR_BLOB_CONVERTER,
    COSMO_NN_ERR_TRACKER_CREATION,
    COSMO_NN_ERR_TRACKER_CFG,
    COSMO_NN_ERR_NOT_IMPLEMENTED,
    COSMO_NN_ERR_GRAPH_NOT_INIT,

    // device err
    COSMO_NN_ERR_DEVICE_NOT_SUPPORT = 0x4000,
    COSMO_NN_ERR_DEVICE_INVALID_QUEUE,
    COSMO_NN_ERR_DEVICE_CONTEXT_CREATE,

    // json
    COSMO_NN_ERR_JSON_PARSE = 0x6000,
    COSMO_NN_ERR_JSON_INVALID_INPUT,
    COSMO_NN_ERR_JSON_INVALID_BATCH,

    // sophon
    COSMO_NN_ERR_SOPHON_MEMCPY_ERR = 0x9000,
    COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED,
    COSMO_NN_ERR_SOPHON_FREE_MEM_FAILED,
    COSMO_NN_ERR_SOPHON_HANDLE_FAILED,
    COSMO_NN_ERR_SOPHON_INFER_ERR,
    COSMO_NN_ERR_SOPHON_NET_CREATE_FAILED,
    COSMO_NN_ERR_SOPHON_REQUEST_HANDLE_FAILED,
    COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED,
    COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED,
    COSMO_NN_ERR_SOPHON_ATTACH_FAILED,
    COSMO_NN_ERR_SOPHON_DETACH_FAILED,

};

class PUBLIC Status {
public:
    ~Status();

    Status(int c = COSMO_NN_OK, std::string message = "OK");

    Status& operator=(int c);

    bool operator==(int c);
    bool operator!=(int c);

    operator int();
    operator bool();

    std::string description();

private:
    int code            = 0;
    std::string message = "";
};

}  // namespace cosmo::nn
