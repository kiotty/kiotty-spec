#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Key, typename Value>
class FixedMap
{
public:
    explicit FixedMap(size_t capacity) : _keys(capacity), _values(capacity), _length(0) {}

    static constexpr size_t MAX_VALUE_KEY_BYTES = 16;
    static constexpr bool IS_SMALL_KEY =
        sizeof(Key) <= MAX_VALUE_KEY_BYTES && std::is_trivially_copyable<Key>::value;
    using KeyArgument = std::conditional_t<IS_SMALL_KEY, Key, const Key&>;

    KiottySpecStatus insert(KeyArgument key, Value value)
    {
        const size_t position = lowerBound(_keys, _length, key);
        const bool has_key = hasKeyAt(_keys, _length, position, key);
        if (has_key) { return KIOTTY_SPEC_STATUS_CONFLICT; }
        const bool is_full = isFull(_length, _keys.size());
        if (is_full) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = _length; index > position; --index) {
            _keys[index] = _keys[index - 1];
            _values[index] = std::move(_values[index - 1]);
        }
        _keys[position] = key;
        _values[position] = std::move(value);
        ++_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Result<Value, KiottySpecStatus> find(KeyArgument key) const
    {
        const size_t position = lowerBound(_keys, _length, key);
        const bool is_missing = isMissingAt(_keys, _length, position, key);
        if (is_missing) {
            return Result<Value, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
        }
        return Result<Value, KiottySpecStatus>::success(_values[position]);
    }

    KiottySpecStatus erase(KeyArgument key)
    {
        const size_t position = lowerBound(_keys, _length, key);
        const bool is_missing = isMissingAt(_keys, _length, position, key);
        if (is_missing) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
        eraseAt(viewVector(_keys), viewVector(_values), position, _length);
        --_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear() { _length = 0; }

    size_t length() const { return _length; }
    const Key& keyAt(size_t index) const { return _keys[index]; }
    const Value& valueAt(size_t index) const { return _values[index]; }
    Value& valueAt(size_t index) { return _values[index]; }

private:
    static size_t lowerBound(const std::vector<Key>& keys, size_t length, KeyArgument key)
    {
        constexpr size_t BISECT_DIVISOR = 2;
        size_t low = 0;
        size_t high = length;
        while (low < high) {
            const size_t middle = low + (high - low) / BISECT_DIVISOR;
            if (keys[middle] < key) { low = middle + 1; } else { high = middle; }
        }
        return low;
    }

    static bool hasKeyAt(const std::vector<Key>& keys, size_t length, size_t position,
                         KeyArgument key)
    { return position < length && !(key < keys[position]); }

    static bool isMissingAt(const std::vector<Key>& keys, size_t length, size_t position,
                            KeyArgument key)
    { return position == length || key < keys[position]; }

    static bool isFull(size_t length, size_t capacity) { return length == capacity; }

    static void eraseAt(Span<Key> keys, Span<Value> values, size_t position, size_t length)
    {
        for (size_t index = position + 1; index < length; ++index) {
            keys[index - 1] = keys[index];
            values[index - 1] = std::move(values[index]);
        }
    }

    std::vector<Key> _keys;
    std::vector<Value> _values;
    size_t _length;
};

}
