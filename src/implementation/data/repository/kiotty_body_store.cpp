#include "implementation/data/repository/kiotty_body_store.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using SliceMap = FixedMap<uint32_t, BodySlice>;
using SliceResult = Result<BodySlice, KiottySpecStatus>;

bool isShortOfSpace(size_t capacity, size_t used, size_t length);
SliceResult writeBody(Span<char> bytes, Span<const char> body, size_t offset);
Span<const char> viewSlice(Span<const char> bytes, BodySlice slice);
void sortByOffset(Span<uint32_t> order, const SliceMap& slices);
BodySlice copySlice(Span<char> bytes, BodySlice slice, size_t offset);

}

KiottySpecStatus BodyStore::put(uint32_t key, Span<const char> body)
{
    erase(key);
    const size_t length = body.length();
    if (length == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const size_t slice_count = _slices.length();
    const size_t slice_capacity = _order.size();
    if (slice_count == slice_capacity) { return KIOTTY_SPEC_STATUS_POOL_IS_FULL; }
    const size_t byte_capacity = _bytes.size();
    const bool is_short = isShortOfSpace(byte_capacity, _used, length);
    if (is_short) { compact(); }
    const bool is_still_short = isShortOfSpace(byte_capacity, _used, length);
    if (is_still_short) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const Span<char> bytes = viewVector(_bytes);
    const SliceResult written = writeBody(bytes, body, _used);
    if (!written) { return written.code; }
    const KiottySpecStatus inserted = _slices.insert(key, written.value);
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) { return inserted; }
    _used += length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Span<const char> BodyStore::find(uint32_t key) const
{
    const auto slice = _slices.find(key);
    if (!slice) { return Span<const char>(); }
    const Span<const char> bytes = viewVector(_bytes);
    return viewSlice(bytes, slice.value);
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
    const Span<const char> bytes = viewVector(_bytes);
    return viewSlice(bytes, slice);
}

void BodyStore::compact()
{
    const size_t count = _slices.length();
    const Result<Span<uint32_t>, KiottySpecStatus> order = viewVector(_order).subspan(0, count);
    sortByOffset(order.value, _slices);
    const Span<char> bytes = viewVector(_bytes);
    size_t used = 0;
    for (size_t position = 0; position < count; ++position) {
        BodySlice& slice = _slices.valueAt(order.value[position]);
        slice = copySlice(bytes, slice, used);
        used += slice.length;
    }
    _used = used;
}

Result<size_t, KiottySpecStatus> copyBody(Span<const char> body, Span<char> buffer)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const size_t length = body.length();
    const bool is_too_small = buffer.length() <= length;
    if (is_too_small) { return SizeResult { KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, length + 1 }; }
    const KiottySpecStatus copied = buffer.copyFrom(body);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SizeResult { copied, length + 1 }; }
    const Result<char&, KiottySpecStatus> terminator = buffer.at(length);
    if (!terminator) { return SizeResult { KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, length + 1 }; }
    *terminator.value = '\0';
    return SizeResult::success(length);
}

namespace {

bool isShortOfSpace(size_t capacity, size_t used, size_t length)
{ return length > capacity - used; }

SliceResult writeBody(Span<char> bytes, Span<const char> body, size_t offset)
{
    const Result<Span<char>, KiottySpecStatus> target = bytes.subspan(offset, body.length());
    if (!target) { return SliceResult::failure(target.code); }
    const KiottySpecStatus copied = target.value.copyFrom(body);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SliceResult::failure(copied); }
    return SliceResult::success(BodySlice{offset, body.length()});
}

Span<const char> viewSlice(Span<const char> bytes, BodySlice slice)
{ return Span<const char>(bytes.begin() + slice.offset, slice.length); }

void sortByOffset(Span<uint32_t> order, const SliceMap& slices)
{
    const size_t length = order.length();
    for (size_t index = 0; index < length; ++index) { order[index] = static_cast<uint32_t>(index); }
    std::sort(order.begin(), order.end(), [&slices](uint32_t left, uint32_t right) {
        return slices.valueAt(left).offset < slices.valueAt(right).offset;
    });
}

BodySlice copySlice(Span<char> bytes, BodySlice slice, size_t offset)
{
    const size_t first = slice.offset;
    const size_t last = first + slice.length;
    std::copy(bytes.begin() + first, bytes.begin() + last, bytes.begin() + offset);
    return BodySlice{offset, slice.length};
}

}

}
