#include "implementation/data/repository/kiotty_item_reader.h"

#include "implementation/data/repository/kiotty_body_store.h"

namespace kiotty::spec::implementation {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemRecord;

Result<ItemHandle, KiottySpecStatus> ItemReader::find(Span<const char> id) const
{
    IdText key {};
    const KiottySpecStatus assigned = key.assign(id);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    const auto index = _t.id_index.find(key);
    if (!index) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemHandle, KiottySpecStatus>::success(_t.items.handleAt(index.value));
}

Result<ItemHandle, KiottySpecStatus> ItemReader::findKey(uint32_t key) const
{
    const auto index = _t.key_index.find(key);
    if (!index) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemHandle, KiottySpecStatus>::success(_t.items.handleAt(index.value));
}

Result<const ItemRecord*, KiottySpecStatus> ItemReader::get(ItemHandle handle) const
{
    const auto record = _t.items.get(handle);
    if (!record) { return Result<const ItemRecord*, KiottySpecStatus>::failure(record.code); }
    return Result<const ItemRecord*, KiottySpecStatus>::success(record.value);
}

uint32_t ItemReader::key(ItemHandle handle) const
{
    const auto record = _t.items.get(handle);
    if (!record) { return entity::NO_KEY; }
    return _t.keys[handle.index];
}

Result<size_t, KiottySpecStatus> ItemReader::readBody(ItemHandle handle, char* buffer,
                                                      size_t capacity) const
{
    const auto record = _t.items.get(handle);
    if (!record) {
        return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE);
    }
    return copyBody(_t.bodies.find(_t.keys[handle.index]), buffer, capacity);
}

}
