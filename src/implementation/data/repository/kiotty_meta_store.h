#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t MAX_META_KEY_LENGTH = 63;
constexpr size_t MAX_META_VALUE_LENGTH = 255;
constexpr size_t MAX_META_ENTRIES = 32;
using MetaKey = FixedString<MAX_META_KEY_LENGTH>;
using MetaValue = FixedString<MAX_META_VALUE_LENGTH>;

class MetaStore
{
public:
    MetaStore() : _entries(MAX_META_ENTRIES) {}

    void clear() { _entries.clear(); }
    KiottySpecStatus set(Span<const char> key, Span<const char> value);
    Result<MetaValue, KiottySpecStatus> get(Span<const char> key) const;
    const FixedMap<MetaKey, MetaValue>& all() const { return _entries; }

private:
    FixedMap<MetaKey, MetaValue> _entries;
};

}
