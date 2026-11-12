#pragma once

#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

size_t countAcceptanceCriteria(Span<const char> body);

}
