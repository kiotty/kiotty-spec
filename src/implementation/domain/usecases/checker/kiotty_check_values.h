#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::check_values {

inline uint64_t readValue(Span<const uint64_t> values, size_t index)
{
    const Result<const uint64_t&, KiottySpecStatus> value = values.at(index);
    return value ? *value.value : 0;
}

inline uint32_t readCount(Span<const uint32_t> counts, size_t index)
{
    const Result<const uint32_t&, KiottySpecStatus> count = counts.at(index);
    return count ? *count.value : 0;
}

inline void clearValues(Span<uint64_t> values)
{
    for (uint64_t& value : values) { value = 0; }
}

}
