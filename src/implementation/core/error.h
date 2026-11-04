#pragma once

#include "kiotty_spec.h"

#include <stdexcept>
#include <string>

namespace kiotty::core {

// 구현 내부의 모든 실패는 이 예외로 표현하고, C 경계에서 KiottySpecStatus 로 바꾼다.
class Error : public std::runtime_error {
public:
    Error(KiottySpecStatus status, const std::string &message)
        : std::runtime_error(message), status_(status) {}

    KiottySpecStatus status() const noexcept { return status_; }

private:
    KiottySpecStatus status_;
};

[[noreturn]] inline void fail(KiottySpecStatus status, const std::string &message) {
    throw Error(status, message);
}

inline void require(bool condition, KiottySpecStatus status, const std::string &message) {
    if (!condition) {
        fail(status, message);
    }
}

} // namespace kiotty::core
