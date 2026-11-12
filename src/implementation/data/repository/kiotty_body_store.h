#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct BodySlice {
    size_t offset {};
    size_t length {};
};

static_assert(sizeof(BodySlice) == 16, "unexpected padding");

class BodyStore
{
public:
    BodyStore(size_t bytes, size_t keys) : _bytes(bytes), _slices(keys), _order(keys) {}

    KiottySpecStatus put(uint32_t key, Span<const char> body);
    Span<const char> find(uint32_t key) const;
    void erase(uint32_t key);
    void clear();
    size_t length() const { return _slices.length(); }
    uint32_t keyAt(size_t index) const { return _slices.keyAt(index); }
    Span<const char> bodyAt(size_t index) const;

private:
    void compact();

    std::vector<char> _bytes;
    FixedMap<uint32_t, BodySlice> _slices;
    std::vector<uint32_t> _order;
    size_t _used {0};
};

Result<size_t, KiottySpecStatus> copyBody(Span<const char> body, char* buffer, size_t capacity);

}
