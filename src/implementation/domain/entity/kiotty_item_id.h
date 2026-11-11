#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"

namespace kiotty::spec::implementation::entity {

KiottySpecStatus validateItemId(Span<const char> id);

}
