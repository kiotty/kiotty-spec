#pragma once

#include "kiotty_spec.h"

#include <stdexcept>
#include <string>

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

}
