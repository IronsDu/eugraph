#pragma once

#include <cstdint>
#include <string>

namespace eugraph {

enum class ErrorCode : uint8_t {
    OK = 0,
    NOT_FOUND,
    ALREADY_EXISTS,
    INVALID_ARGUMENT,
    IO_ERROR,
    INTERNAL_ERROR,
    TRANSACTION_CONFLICT,
};

class Error {
public:
    Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    ErrorCode code() const {
        return code_;
    }
    const std::string& message() const {
        return message_;
    }

private:
    ErrorCode code_;
    std::string message_;
};

} // namespace eugraph
