// Public exception classes for Cosmo.

#pragma once

#include <exception>
#include <string>
#include <system_error>

namespace cosmo::util {

class Exception : public std::exception {
public:
    explicit Exception(const std::string& msg) noexcept;

    [[nodiscard]] const char* what() const noexcept override;

protected:
    Exception() noexcept = default;
    void SetMessage(std::string&& msg) noexcept;

private:
    std::string msg_;
};

class ErrorMessage : public Exception {
public:
    explicit ErrorMessage(std::error_condition errc) noexcept;
    ErrorMessage(std::error_condition errc, const char* msg) noexcept;

    [[nodiscard]] std::error_condition GetValue() const noexcept;

private:
    std::error_condition errc_;
};

}  // namespace cosmo::util
