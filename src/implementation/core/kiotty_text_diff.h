#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DIFF_TEXT_BYTES = 255;

enum class TextChange : uint8_t { UNCHANGED = 0, ADDED = 1, REMOVED = 2 };
enum class TextSource : uint8_t { BEFORE = 0, AFTER = 1 };

struct TextSegment {
    TextChange change {};
    TextSource source {};
    uint16_t offset {};
    uint16_t length {};
    uint16_t reserved {};
};

static_assert(sizeof(TextSegment) == 8, "unexpected padding");

Result<size_t, KiottySpecStatus> diffText(Span<const char> before, Span<const char> after, Span<uint8_t> table, Span<TextSegment> out);

}
