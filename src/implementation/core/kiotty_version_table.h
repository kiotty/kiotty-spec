#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Record>
class VersionTable
{
public:
    using Identity = typename Record::Identity;

    explicit VersionTable(size_t capacity) : _records(capacity), _length(0) {}

    void clear() { _length = 0; }

    KiottySpecStatus append(const Record& record)
    {
        const bool is_full = isFull(_length, _records.size());
        if (is_full) { return KIOTTY_SPEC_STATUS_POOL_IS_FULL; }
        _records[_length] = record;
        ++_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void sort()
    {
        const Span<Record> records = viewRecords();
        std::sort(records.begin(), records.end(), less);
    }

    Result<size_t, KiottySpecStatus> findDuplicate() const { return findDuplicateIn(all()); }

    KiottySpecStatus write(const Record& record)
    {
        const auto target = slot(record.identity(), record.checkpoint);
        if (!target) { return target.code; }
        *target.value = record;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Result<Record&, KiottySpecStatus> slot(Identity identity, uint32_t checkpoint)
    {
        const size_t position = lowerBound(all(), identity, checkpoint);
        const bool is_found = isRecordAt(all(), position, identity, checkpoint);
        if (is_found) { return Result<Record&, KiottySpecStatus>::success(_records[position]); }
        const bool is_full = isFull(_length, _records.size());
        if (is_full) {
            return Result<Record&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL);
        }
        for (size_t index = _length; index > position; --index) {
            _records[index] = _records[index - 1];
        }
        _records[position] = emptyRecord();
        _records[position].checkpoint = checkpoint;
        ++_length;
        return Result<Record&, KiottySpecStatus>::success(_records[position]);
    }

    const Record* latest(Identity identity, uint32_t checkpoint) const
    {
        const size_t after = lowerBound(all(), identity, static_cast<uint64_t>(checkpoint) + 1);
        if (after == 0) { return nullptr; }
        const Identity last_identity = _records[after - 1].identity();
        const bool is_same_identity = last_identity == identity;
        if (is_same_identity) { return &_records[after - 1]; }
        return nullptr;
    }

    size_t countAt(uint32_t checkpoint) const { return countRecordsAt(all(), checkpoint); }

    void prune(uint32_t base) { _length = pruneRecords(viewRecords(), base); }

    template <typename Keep>
    void retain(Keep keep)
    {
        _length = retainRecords(viewRecords(), keep);
    }

    Span<const Record> all() const { return Span<const Record>(_records.data(), _length); }

private:
    static bool less(const Record& left, const Record& right)
    {
        const Identity left_identity = left.identity();
        const Identity right_identity = right.identity();
        if (left_identity < right_identity) { return true; }
        if (right_identity < left_identity) { return false; }
        return left.checkpoint < right.checkpoint;
    }

    static const Record& emptyRecord()
    {
        static const Record empty_record {};
        return empty_record;
    }

    static bool isFull(size_t length, size_t capacity) { return length == capacity; }

    static Result<size_t, KiottySpecStatus> findDuplicateIn(Span<const Record> records)
    {
        const size_t length = records.length();
        for (size_t index = 1; index < length; ++index) {
            const bool is_same = records[index - 1].identity() == records[index].identity();
            if (is_same && records[index - 1].checkpoint == records[index].checkpoint) {
                return Result<size_t, KiottySpecStatus>::success(index);
            }
        }
        return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }

    static bool isRecordAt(Span<const Record> records, size_t position, Identity identity,
                           uint32_t checkpoint)
    {
        const size_t length = records.length();
        if (position >= length) { return false; }
        const Record& record = records[position];
        return record.identity() == identity && record.checkpoint == checkpoint;
    }

    static size_t countRecordsAt(Span<const Record> records, uint32_t checkpoint)
    {
        size_t count = 0;
        const size_t length = records.length();
        for (size_t index = 0; index < length; ++index) {
            count += records[index].checkpoint == checkpoint ? 1 : 0;
        }
        return count;
    }

    static size_t pruneRecords(Span<Record> records, uint32_t base)
    {
        size_t kept = 0;
        size_t first = 0;
        const size_t length = records.length();
        while (first < length) {
            const size_t end = findGroupEnd(records, first);
            kept = pruneGroup(records, first, end, base, kept);
            first = end;
        }
        return kept;
    }

    template <typename Keep>
    static size_t retainRecords(Span<Record> records, Keep keep)
    {
        size_t kept = 0;
        const size_t length = records.length();
        for (size_t index = 0; index < length; ++index) {
            const bool is_kept = keep(records[index]);
            if (!is_kept) { continue; }
            if (kept != index) { records[kept] = records[index]; }
            ++kept;
        }
        return kept;
    }

    static size_t lowerBound(Span<const Record> records, Identity identity, uint64_t checkpoint)
    {
        constexpr size_t BISECT_DIVISOR = 2;
        size_t low = 0;
        size_t high = records.length();
        while (low < high) {
            const size_t middle = low + (high - low) / BISECT_DIVISOR;
            const Record& record = records[middle];
            const Identity record_identity = record.identity();
            const bool is_before =
                record_identity < identity ||
                (record_identity == identity && record.checkpoint < checkpoint);
            if (is_before) { low = middle + 1; } else { high = middle; }
        }
        return low;
    }

    static size_t findGroupEnd(Span<const Record> records, size_t first)
    {
        const size_t length = records.length();
        const Identity group_identity = records[first].identity();
        for (size_t end = first + 1; end < length; ++end) {
            const Identity end_identity = records[end].identity();
            const bool is_same_group = end_identity == group_identity;
            if (is_same_group) { continue; }
            return end;
        }
        return length;
    }

    static size_t pruneGroup(Span<Record> records, size_t first, size_t end, uint32_t base,
                             size_t kept)
    {
        size_t last_old = end;
        for (size_t index = first; index < end && records[index].checkpoint <= base; ++index) {
            last_old = index;
        }
        size_t next_kept = kept;
        for (size_t index = first; index < end; ++index) {
            const bool is_older = records[index].checkpoint <= base;
            const bool is_survivor = index == last_old && records[index].removed == 0;
            const bool is_kept = is_survivor || records[index].checkpoint > base;
            if (!is_kept) { continue; }
            if (next_kept != index) { records[next_kept] = records[index]; }
            if (is_older) { records[next_kept].checkpoint = base; }
            ++next_kept;
        }
        return next_kept;
    }

    Span<Record> viewRecords() { return Span<Record>(_records.data(), _length); }

    std::vector<Record> _records;
    size_t _length;
};

}
