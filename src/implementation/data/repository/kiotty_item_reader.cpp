#include "implementation/data/repository/kiotty_item_reader.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char BODY_SQL[] =
    "SELECT body FROM item_version WHERE item_id = ?1 AND removed = 0 ORDER BY checkpoint DESC LIMIT 1";

}

using entity::IdText;
using entity::ItemHandle;
using entity::ItemRecord;

Result<ItemHandle, KiottySpecStatus> ItemReader::find(Span<const char> id) const
{
    IdText key;
    if (key.assign(id) != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    const auto index = _id_index.find(key);
    if (!index) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return Result<ItemHandle, KiottySpecStatus>::success(_items.handleAt(index.value));
}

Result<const ItemRecord*, KiottySpecStatus> ItemReader::get(ItemHandle handle) const
{
    const auto record = _items.get(handle);
    if (!record) { return Result<const ItemRecord*, KiottySpecStatus>::failure(record.code); }
    return Result<const ItemRecord*, KiottySpecStatus>::success(record.value);
}

Result<size_t, KiottySpecStatus> ItemReader::readBody(ItemHandle handle, char* buffer, size_t capacity) const
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const auto stored = _items.get(handle);
    if (!stored) { return SizeResult::failure(stored.code); }
    auto query = _storage.prepare(literalText(BODY_SQL));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, stored.value->id.view());
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_storage.check(stepped.code)); }
    const Span<const char> body = stepped.value ? query.value.row().text(0) : Span<const char>();
    if (buffer == nullptr || capacity <= body.length()) { return SizeResult{KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, body.length() + 1}; }
    for (size_t index = 0; index < body.length(); ++index) { buffer[index] = body[index]; }
    buffer[body.length()] = '\0';
    return SizeResult::success(body.length());
}

}
