#include "implementation/data/repository/kiotty_item_reader.h"

#include "implementation/data/repository/kiotty_item_history.h"

namespace kiotty::spec::implementation {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemRecord;

Result<size_t, KiottySpecStatus> readBodyFile(Span<const char> directory, uint32_t key, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const SystemPathResult path = bodyPath(directory, key);
    if (!path) { return SizeResult::failure(path.code); }
    const auto length = fileLength(path.value.view());
    const size_t size = length ? length.value : 0;
    if (buffer == nullptr || capacity <= size) { return SizeResult{KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, size + 1}; }
    buffer[0] = '\0';
    if (!length) { return SizeResult::success(0); }
    const auto read = readFile(path.value.view(), Span<char>(buffer, capacity - 1));
    if (!read) { return SizeResult::failure(read.code); }
    buffer[read.value] = '\0';
    return SizeResult::success(read.value);
}

Result<ItemHandle, KiottySpecStatus> ItemReader::find(Span<const char> id) const
{
    IdText key;
    if (key.assign(id) != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    const auto index = _t.id_index.find(key);
    if (!index) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return Result<ItemHandle, KiottySpecStatus>::success(_t.items.handleAt(index.value));
}

Result<ItemHandle, KiottySpecStatus> ItemReader::findKey(uint32_t key) const
{
    const auto index = _t.key_index.find(key);
    if (!index) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
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
    if (!_t.items.get(handle)) { return entity::NO_KEY; }
    return _t.keys[handle.index];
}

Result<size_t, KiottySpecStatus> ItemReader::readBody(ItemHandle handle, char* buffer, size_t capacity) const
{
    if (!_t.items.get(handle)) { return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE); }
    return readBodyFile(_t.body_directory.view(), _t.keys[handle.index], buffer, capacity);
}

}
