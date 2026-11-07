#pragma once

#include "kiotty_spec.h"
#include "failure.h"
#include "result.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace kiotty::spec::implementation {

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

inline void expect(KiottySpecStatus status, const Failure &failure) {
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(status, std::string(failure.message().data(), failure.message().length()));
    }
}

template <typename Value>
Value expect(Result<Value, KiottySpecStatus> result, const Failure &failure) {
    expect(result.code, failure);
    return std::move(result.value);
}

}
