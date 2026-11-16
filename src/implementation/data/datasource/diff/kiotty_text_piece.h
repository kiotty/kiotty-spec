#pragma once

#include "implementation/core/kiotty_span.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct TextPiece {
    Span<const char> text {};
    uint32_t change {0};
};

static_assert(sizeof(TextPiece) == 24, "unexpected padding");

struct TextPair {
    Span<const char> before {};
    Span<const char> after {};
};

static_assert(sizeof(TextPair) == 32, "unexpected padding");

}
