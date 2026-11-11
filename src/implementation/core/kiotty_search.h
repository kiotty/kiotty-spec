#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct IndexRange {
    size_t first {0};
    size_t length {0};
};

template <typename Element, typename Match>
Result<IndexRange, KiottySpecStatus> contiguousRange(Span<const Element> elements, Match match)
{
    IndexRange range {};
    bool found = false;
    const size_t element_count = elements.length();
    for (size_t index = 0; index < element_count; ++index) {
        const bool is_match = match(elements[index]);
        if (!is_match) { continue; }
        if (found && index != range.first + range.length) {
            return Result<IndexRange, KiottySpecStatus>::failure(
                KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
        }
        range.first = found ? range.first : index;
        found = true;
        ++range.length;
    }
    return Result<IndexRange, KiottySpecStatus>::success(range);
}

template <typename Element, typename Match>
Result<uint32_t, KiottySpecStatus> findIndex(Span<const Element> elements, Match match)
{
    const size_t element_count = elements.length();
    for (size_t index = 0; index < element_count; ++index) {
        const bool is_match = match(elements[index]);
        if (is_match) {
            return Result<uint32_t, KiottySpecStatus>::success(static_cast<uint32_t>(index));
        }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
}

}
