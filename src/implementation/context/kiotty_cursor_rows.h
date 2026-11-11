#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t PACKED_ID_WORD_BYTES = 8;
constexpr size_t PACKED_ID_WORDS = 2;
constexpr size_t PACKED_ID_BYTES = PACKED_ID_WORD_BYTES * PACKED_ID_WORDS;
constexpr uint32_t BITS_PER_BYTE = 8;
constexpr uint64_t BYTE_MASK = 0xFFu;

inline KiottySpecStatus raiseCursorFull(entity::Failure& failure, KiottySpecStatus status)
{
    return failure.raise(status, KIOTTY_SPEC_SUBJECT_CURSOR, literalText("max_cursors"));
}

inline KiottySpecStatus pushRow(CursorPool& cursors, entity::Failure& failure, CursorId cursor,
                                const CursorRow& row)
{
    const KiottySpecStatus pushed = cursors.push(cursor, row);
    if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        return failure.raise(pushed, KIOTTY_SPEC_SUBJECT_CURSOR_ROWS,
                             literalText("max_cursor_rows"));
    }
    return pushed;
}

inline CursorIdResult closeWith(CursorPool& cursors, CursorId cursor, KiottySpecStatus status)
{
    cursors.close(cursor);
    return CursorIdResult::failure(status);
}

inline uint64_t packIdWord(entity::IdText id, size_t offset)
{
    const Span<const char> text = id.view();
    uint64_t word {0};
    for (size_t index = 0; index < PACKED_ID_WORD_BYTES; ++index) {
        const Result<const char&, KiottySpecStatus> letter = text.at(offset + index);
        const uint64_t byte = letter ? static_cast<unsigned char>(*letter.value) : 0u;
        word |= byte << (BITS_PER_BYTE * index);
    }
    return word;
}

inline entity::IdText unpackId(uint64_t first, uint64_t second)
{
    char bytes[PACKED_ID_BYTES] {};
    const Span<char> letters(bytes, PACKED_ID_BYTES);
    const size_t letter_count = letters.length();
    for (size_t index = 0; index < letter_count; ++index) {
        const uint64_t word = index < PACKED_ID_WORD_BYTES ? first : second;
        const size_t shift = BITS_PER_BYTE * (index % PACKED_ID_WORD_BYTES);
        letters[index] = static_cast<char>((word >> shift) & BYTE_MASK);
    }
    entity::IdText id {};
    id.assign(boundedText(bytes, entity::MAX_ID_LENGTH));
    return id;
}

}
