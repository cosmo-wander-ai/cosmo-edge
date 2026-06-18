#include "nn/core/status.h"

#include <iomanip>
#include <sstream>

namespace cosmo::nn {

std::string StatusGetDefaultMessage(int code) {
    switch (code) {
        case COSMO_NN_ERR_DEVICE_NOT_SUPPORT:
            return "device is null or unsupported";
        default:
            return "";
    }
}

Status::~Status() {
    code    = 0;
    message = "";
}

Status::Status(int c, std::string msg) {
    code    = c;
    message = msg;
}

Status& Status::operator=(int c) {
    code    = c;
    message = StatusGetDefaultMessage(code);
    return *this;
}

bool Status::operator==(int c) {
    return code == c;
}

bool Status::operator!=(int c) {
    return code != c;
}

Status::operator bool() {
    return code == COSMO_NN_OK;
}

Status::operator int() {
    return code;
}

std::string Status::description() {
    std::ostringstream os;
    os << "code:0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << code
       << " msg: " << message;
    return os.str();
}

}  // namespace cosmo::nn