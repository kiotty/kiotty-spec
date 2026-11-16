#pragma once

#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstdint>

namespace kiotty::spec::implementation::entity {

using CursorId = uint64_t;

CursorId encodeCursor(CursorHandle handle);
CursorHandle decodeCursor(CursorId cursor);
uint64_t packHandle(ItemHandle handle);
ItemHandle unpackHandle(uint64_t value);

}
