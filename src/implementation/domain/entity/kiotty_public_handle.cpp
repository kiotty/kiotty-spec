#include "implementation/domain/entity/kiotty_public_handle.h"

namespace kiotty::spec::implementation::entity {

namespace {

constexpr uint32_t GENERATION_SHIFT = 32;
constexpr uint64_t INDEX_MASK = 0xFFFFFFFFu;
constexpr uint64_t MAX_GENERATION = 0xFFFFu;

}

CursorId encodeCursor(CursorHandle handle)
{
    return (static_cast<uint64_t>(handle.generation) << GENERATION_SHIFT) |
           (static_cast<uint64_t>(handle.index) + 1);
}

CursorHandle decodeCursor(CursorId cursor)
{
    const uint64_t index = cursor & INDEX_MASK;
    const uint64_t generation = cursor >> GENERATION_SHIFT;
    const bool is_out_of_range = index == 0 || generation > MAX_GENERATION;
    if (is_out_of_range) { return CursorHandle {}; }
    return CursorHandle {static_cast<uint32_t>(index - 1), static_cast<uint16_t>(generation)};
}

uint64_t packHandle(ItemHandle handle)
{
    return (static_cast<uint64_t>(handle.generation) << GENERATION_SHIFT) | handle.index;
}

ItemHandle unpackHandle(uint64_t value)
{
    const uint64_t generation = value >> GENERATION_SHIFT;
    const bool is_out_of_range = generation > MAX_GENERATION;
    if (is_out_of_range) { return ItemHandle {}; }
    return ItemHandle {static_cast<uint32_t>(value & INDEX_MASK),
                       static_cast<uint16_t>(generation)};
}

}
