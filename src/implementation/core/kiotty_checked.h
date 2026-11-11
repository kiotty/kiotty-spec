#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"

#include <cstddef>
#include <limits>

namespace kiotty::spec::implementation {

inline Result<size_t, KiottySpecStatus> checkedMultiply(size_t left, size_t right)
{
    if (left != 0 && right > std::numeric_limits<size_t>::max() / left) {
        return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW);
    }
    return Result<size_t, KiottySpecStatus>::success(left * right);
}

}
