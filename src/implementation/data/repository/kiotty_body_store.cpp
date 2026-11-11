#include "implementation/data/repository/kiotty_body_store.h"

#include <algorithm>

namespace kiotty::spec::implementation {

KiottySpecStatus BodyStore::put(uint32_t key, Span<const char> body)
{
    erase(key);
    const size_t length = body.length();
    if (length == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const size_t slice_count = _slices.length();
    const size_t slice_capacity = _slices.capacity();
    if (slice_count == slice_capacity) { return KIOTTY_SPEC_STATUS_POOL_IS_FULL; }
    const size_t byte_capacity = _bytes.size();
    if (length > byte_capacity - _used) { compact(); }
    if (length > byte_capacity - _used) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    std::copy(body.begin(), body.end(), _bytes.begin() + static_cast<std::ptrdiff_t>(_used));
    const KiottySpecStatus inserted = _slices.insert(key, BodySlice{_used, body.length()});
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) { return inserted; }
    _used += body.length();
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Span<const char> BodyStore::find(uint32_t key) const
{
    const auto slice = _slices.find(key);
    if (!slice) { return Span<const char>(); }
    return Span<const char>(_bytes.data() + slice.value.offset, slice.value.length);
}

void BodyStore::erase(uint32_t key) { _slices.erase(key); }

void BodyStore::clear()
{
    _slices.clear();
    _used = 0;
}

Span<const char> BodyStore::bodyAt(size_t index) const
{
    const BodySlice& slice = _slices.valueAt(index);
    return Span<const char>(_bytes.data() + slice.offset, slice.length);
}

void BodyStore::compact()
{
    const size_t count = _slices.length();
    for (size_t index = 0; index < count; ++index) { _order[index] = static_cast<uint32_t>(index); }
    const FixedMap<uint32_t, BodySlice>& slices = _slices;
    std::sort(_order.begin(), _order.begin() + static_cast<std::ptrdiff_t>(count),
              [&slices](uint32_t left, uint32_t right) {
                  return slices.valueAt(left).offset < slices.valueAt(right).offset;
              });
    size_t used = 0;
    for (size_t position = 0; position < count; ++position) {
        BodySlice& slice = _slices.valueAt(_order[position]);
        const auto first = _bytes.begin() + static_cast<std::ptrdiff_t>(slice.offset);
        std::copy(first, first + static_cast<std::ptrdiff_t>(slice.length),
                  _bytes.begin() + static_cast<std::ptrdiff_t>(used));
        slice.offset = used;
        used += slice.length;
    }
    _used = used;
}

Result<size_t, KiottySpecStatus> copyBody(Span<const char> body, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const size_t length = body.length();
    if (buffer == nullptr || capacity <= length) {
        return SizeResult { KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, length + 1 };
    }
    std::copy(body.begin(), body.end(), buffer);
    buffer[length] = '\0';
    return SizeResult::success(length);
}

}
