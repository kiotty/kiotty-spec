#pragma once

#include "kiotty_spec.h"
#include "kiotty_lazy_pool.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Element, typename HandleValue>
class DetailPool
{
public:
    explicit DetailPool(size_t capacity) : _pool(capacity), _marks(capacity, 0) {}

    template <typename Record, typename Reference>
    Result<HandleValue, KiottySpecStatus> acquire(Span<const Record> records, Reference reference)
    {
        using HandleResult = Result<HandleValue, KiottySpecStatus>;
        const HandleValue first_try = _pool.acquire();
        const bool has_slot = first_try.isValid();
        if (!has_slot) { releaseUnreferenced(records, reference); }
        const HandleValue handle = has_slot ? first_try : _pool.acquire();
        const auto slot = _pool.get(handle);
        if (!slot) { return HandleResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
        *slot.value = _empty;
        return HandleResult::success(handle);
    }

    Result<Element&, KiottySpecStatus> get(HandleValue handle) { return _pool.get(handle); }

    const Element& read(HandleValue handle) const
    {
        const auto found = _pool.find(handle);
        if (!found) { return _empty; }
        return *found.value;
    }

    template <typename Record, typename Reference>
    void releaseUnreferenced(Span<const Record> records, Reference reference)
    {
        const Span<uint8_t> marks = viewVector(_marks);
        markReferenced(marks, records, reference);
        releaseUnmarked();
    }

    void clear() { _pool.releaseAll(); }

private:
    template <typename Record, typename Reference>
    static void markReferenced(Span<uint8_t> marks, Span<const Record> records,
                               Reference reference)
    {
        for (uint8_t& mark : marks) { mark = 0; }
        for (const Record& record : records) {
            const Handle handle = reference(record);
            const Result<uint8_t&, KiottySpecStatus> mark = marks.at(handle.index);
            if (mark) { *mark.value = 1; }
        }
    }

    void releaseUnmarked()
    {
        const size_t capacity = _pool.capacity();
        for (size_t index = 0; index < capacity; ++index) {
            const bool is_referenced = _marks[index] != 0;
            if (is_referenced) { continue; }
            _pool.release(_pool.handleAt(index));
        }
    }

    LazyPool<Element, HandleValue> _pool;
    std::vector<uint8_t> _marks;
    Element _empty {};
};

}
