#pragma once

#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_layout.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct TextPiece {
    Span<const char> text {};
    uint32_t change {0};
};

static_assert(sizeof(TextPiece) == layoutSize(24, 12), "unexpected padding");

struct TextPair {
    Span<const char> before {};
    Span<const char> after {};
};

static_assert(sizeof(TextPair) == layoutSize(32, 16), "unexpected padding");

}
