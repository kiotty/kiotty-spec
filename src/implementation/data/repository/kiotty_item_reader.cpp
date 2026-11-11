#include "implementation/data/repository/kiotty_item_reader.h"

#include "implementation/data/repository/kiotty_body_store.h"

namespace kiotty::spec::implementation {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemRecord;

namespace {

uint32_t readKey(const std::vector<uint32_t>& keys, uint32_t index);

}

Result<ItemHandle, KiottySpecStatus> ItemReader::findKey(uint32_t key) const
{
    const auto index = _tables.key_index.find(key);
    if (!index) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemHandle, KiottySpecStatus>::success(_tables.items.handleAt(index.value));
}

uint32_t ItemReader::key(ItemHandle handle) const
{
    const auto record = _tables.items.get(handle);
    if (!record) { return entity::NO_KEY; }
    return readKey(_tables.keys, handle.index);
}

Result<size_t, KiottySpecStatus> ItemReader::readBody(ItemHandle handle, char* buffer,
                                                      size_t capacity) const
{
    const auto record = _tables.items.get(handle);
    if (!record) {
        return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE);
    }
    const uint32_t key = readKey(_tables.keys, handle.index);
    return copyBody(_tables.bodies.find(key), buffer, capacity);
}

Result<ItemHandle, KiottySpecStatus> ItemReader::find(Span<const char> id) const
{
    IdText key {};
    const KiottySpecStatus assigned = key.assign(id);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    const auto index = _tables.id_index.find(key);
    if (!index) {
        return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemHandle, KiottySpecStatus>::success(_tables.items.handleAt(index.value));
}

Result<const ItemRecord*, KiottySpecStatus> ItemReader::get(ItemHandle handle) const
{
    const auto record = _tables.items.get(handle);
    if (!record) { return Result<const ItemRecord*, KiottySpecStatus>::failure(record.code); }
    return Result<const ItemRecord*, KiottySpecStatus>::success(record.value);
}

namespace {

uint32_t readKey(const std::vector<uint32_t>& keys, uint32_t index)
{
    const Span<const uint32_t> all(keys.data(), keys.size());
    const Result<const uint32_t&, KiottySpecStatus> key = all.at(index);
    return key ? *key.value : entity::NO_KEY;
}

}

}
