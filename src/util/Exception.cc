// Public exception classes for Cosmo.

#include "util/Exception.h"

namespace cosmo::util {

Exception::Exception(const std::string& msg) noexcept : msg_(msg) {}

void Exception::SetMessage(std::string&& msg) noexcept {
    msg_ = std::move(msg);
}

const char* Exception::what() const noexcept {
    return msg_.c_str();
}

ErrorMessage::ErrorMessage(std::error_condition errc) noexcept : Exception(errc.message()), errc_(errc) {}

ErrorMessage::ErrorMessage(std::error_condition errc, const char* msg) noexcept
    : Exception(msg), errc_(errc) {}

std::error_condition ErrorMessage::GetValue() const noexcept {
    return errc_;
}

}  // namespace cosmo::util
