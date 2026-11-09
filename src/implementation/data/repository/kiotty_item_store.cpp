#include "implementation/data/repository/kiotty_item_store.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::TagRecord;

constexpr size_t MAX_TAG_TEXT_LENGTH = 8191;
constexpr size_t MAX_NUMBER_DIGITS = 20;
using TagText = FixedString<MAX_TAG_TEXT_LENGTH>;

constexpr char LOAD_SQL[] =
    "SELECT v.item_id, v.checkpoint, v.kind, v.parent_id, v.title, v.summary, v.tags FROM item_version v "
    "JOIN (SELECT item_id, MAX(checkpoint) AS latest FROM item_version GROUP BY item_id) m "
    "ON v.item_id = m.item_id AND v.checkpoint = m.latest WHERE v.removed = 0 ORDER BY v.item_id";

constexpr char WRITE_SQL[] =
    "INSERT OR REPLACE INTO item_version (item_id, checkpoint, kind, parent_id, title, summary, body, tags, removed) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, COALESCE(?7, (SELECT body FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 "
    "ORDER BY checkpoint DESC LIMIT 1), ''), ?8, ?9)";

constexpr char BODY_SQL[] =
    "SELECT body FROM item_version WHERE item_id = ?1 AND removed = 0 ORDER BY checkpoint DESC LIMIT 1";

bool lessTag(const TagRecord& left, const TagRecord& right)
{
    if (left.item.index != right.item.index) { return left.item.index < right.item.index; }
    return left.tag < right.tag;
}

KiottySpecStatus appendNumber(TagText& text, uint64_t number)
{
    char digits[MAX_NUMBER_DIGITS] {};
    size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + number % 10);
        number /= 10;
        ++count;
    } while (number > 0);
    for (size_t index = count; index > 0; --index) {
        const KiottySpecStatus status = text.append(Span<const char>(&digits[index - 1], 1));
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus appendPair(TagText& text, uint32_t tag, uint64_t value)
{
    KiottySpecStatus status = text.append(text.length() > 1 ? literalText(",[") : literalText("["));
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendNumber(text, tag); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(literalText(",")); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendNumber(text, value); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(literalText("]")); }
    return status;
}

// "[[1,4],[2,1]]" 을 읽는다. 숫자 두 개씩 묶는다. 형식이 어긋나면 false
template <typename Visit>
bool parseTagText(Span<const char> text, Visit visit)
{
    uint64_t numbers[2] {};
    size_t count = 0;
    bool in_number = false;
    uint64_t current = 0;
    for (const char letter : text) {
        if (letter >= '0' && letter <= '9') {
            current = current * 10 + static_cast<uint64_t>(letter - '0');
            in_number = true;
            continue;
        }
        if (in_number) {
            if (count == 2) { return false; }
            numbers[count++] = current;
            current = 0;
            in_number = false;
        }
        if (letter == ']' && count == 2) {
            if (!visit(static_cast<uint32_t>(numbers[0]), numbers[1])) { return false; }
            count = 0;
        }
    }
    return count == 0 && !in_number;
}

}

ItemStore::ItemStore(Storage& storage, size_t max_items, size_t max_tags)
    : _storage(storage), _items(max_items), _id_index(max_items), _tags(max_tags)
{
}

KiottySpecStatus ItemStore::load()
{
    _items.releaseAll();
    _id_index.clear();
    _tag_count = 0;
    auto query = _storage.prepare(literalText(LOAD_SQL));
    if (!query) { return query.code; }
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = query.value.row();
        ItemRecord record {};
        record.checkpoint = CheckpointHandle{static_cast<uint32_t>(row.integer(1)), 0};
        record.parent = entity::INVALID_ITEM_HANDLE;
        const uint32_t kind = entity::parseKind(viewOf(row.text(2)));
        if (kind == 0) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("알 수 없는 Kind: "), row.text(2)}); }
        record.kind = static_cast<ItemKind>(kind);
        if (record.id.assign(row.text(0)) != KIOTTY_SPEC_STATUS_SUCCESS || record.title.assign(row.text(4)) != KIOTTY_SPEC_STATUS_SUCCESS ||
            record.summary.assign(row.text(5)) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("항목 필드가 상한을 넘음: "), row.text(0)});
        }
        const ItemHandle handle = _items.acquire();
        if (!handle.isValid()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("항목 수가 max_items 를 넘음")}); }
        *_items.get(handle).value = record;
        const KiottySpecStatus indexed = _id_index.insert(record.id, handle.index);
        if (indexed != KIOTTY_SPEC_STATUS_SUCCESS) { return _storage.reject(indexed, {literalText("ID 색인에 넣을 수 없음: "), row.text(0)}); }
        bool fits = true;
        const bool parsed = parseTagText(row.text(6), [&](uint32_t tag, uint64_t value) {
            if (value == 0) { return true; }
            if (_tag_count == _tags.size()) {
                fits = false;
                return false;
            }
            _tags[_tag_count++] = TagRecord{handle, record.kind, tag, value};
            return true;
        });
        if (!fits) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 를 넘음")}); }
        if (!parsed) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("태그 형식이 잘못됨: "), row.text(0)}); }
    }
    std::sort(_tags.begin(), _tags.begin() + static_cast<std::ptrdiff_t>(_tag_count), lessTag);

    // 상위 항목은 모두 적재한 뒤에 핸들로 바꾼다.
    auto parents = _storage.prepare(literalText(LOAD_SQL));
    if (!parents) { return parents.code; }
    while (true) {
        const auto stepped = parents.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = parents.value.row();
        if (row.isNull(3)) { continue; }
        const auto child = find(row.text(0));
        const auto parent = find(row.text(3));
        if (!child || !parent) { continue; }
        _items.get(child.value).value->parent = parent.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<ItemHandle, KiottySpecStatus> ItemStore::find(Span<const char> id) const
{
    IdText key;
    if (key.assign(id) != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    const auto index = _id_index.find(key);
    if (!index) { return Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return Result<ItemHandle, KiottySpecStatus>::success(_items.handleAt(index.value));
}

Result<const ItemRecord*, KiottySpecStatus> ItemStore::get(ItemHandle handle)
{
    const auto record = _items.get(handle);
    if (!record) { return Result<const ItemRecord*, KiottySpecStatus>::failure(record.code); }
    return Result<const ItemRecord*, KiottySpecStatus>::success(record.value);
}

size_t ItemStore::lowerBound(uint32_t index, uint32_t tag) const
{
    size_t low = 0;
    size_t high = _tag_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const TagRecord& probe = _tags[middle];
        if (probe.item.index < index || (probe.item.index == index && probe.tag < tag)) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

Span<const TagRecord> ItemStore::tagsOf(ItemHandle handle) const
{
    const size_t first = lowerBound(handle.index, 0);
    size_t last = first;
    while (last < _tag_count && _tags[last].item.index == handle.index) { ++last; }
    return Span<const TagRecord>(_tags.data() + first, last - first);
}

uint64_t ItemStore::tagValue(ItemHandle handle, uint32_t tag) const
{
    const size_t position = lowerBound(handle.index, tag);
    if (position < _tag_count && _tags[position].item.index == handle.index && _tags[position].tag == tag) { return _tags[position].value; }
    return 0;
}

KiottySpecStatus ItemStore::writeVersion(const ItemRecord& record, TagEdit edit, const Span<const char>* body, bool removed, uint32_t sequence)
{
    TagText tags;
    tags.assign(literalText("["));
    if (!removed) {
        const auto found = find(record.id.view());
        const Span<const TagRecord> current = found ? tagsOf(found.value) : Span<const TagRecord>();
        bool written = !edit.apply;
        for (const TagRecord& tag : current) {
            if (!written && edit.tag <= tag.tag) {
                if (edit.value != 0) { appendPair(tags, edit.tag, edit.value); }
                written = true;
                if (edit.tag == tag.tag) { continue; }
            }
            appendPair(tags, tag.tag, tag.value);
        }
        if (!written && edit.value != 0) { appendPair(tags, edit.tag, edit.value); }
    }
    if (tags.append(literalText("]")) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }

    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, record.id.view());
    query.value.bind(2, static_cast<int64_t>(sequence));
    query.value.bind(3, spanOf(entity::kindName(record.kind)));
    const auto parent = _items.get(record.parent);
    if (record.parent.isValid() && parent) { query.value.bind(4, parent.value->id.view()); } else { query.value.bind(4, nullptr); }
    query.value.bind(5, record.title.view());
    query.value.bind(6, record.summary.view());
    if (body != nullptr) { query.value.bind(7, *body); } else { query.value.bind(7, nullptr); }
    query.value.bind(8, tags.view());
    query.value.bind(9, static_cast<int64_t>(removed ? 1 : 0));
    return _storage.check(query.value.run());
}

Result<ItemHandle, KiottySpecStatus> ItemStore::create(const ItemRecord& record, uint32_t sequence)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    if (_id_index.find(record.id)) { return HandleResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 있는 ID: "), record.id.view()})); }
    if (_id_index.length() == _id_index.capacity()) {
        return HandleResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("항목 수가 max_items 에 닿음")}));
    }
    const Span<const char> empty(literalText(""));
    const KiottySpecStatus written = writeVersion(record, TagEdit{false, 0, 0}, &empty, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(written); }
    const ItemHandle handle = _items.acquire();
    if (!handle.isValid()) { return HandleResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    ItemRecord& stored = *_items.get(handle).value;
    stored = record;
    stored.checkpoint = CheckpointHandle{sequence, 0};
    _id_index.insert(stored.id, handle.index);
    return HandleResult::success(handle);
}

KiottySpecStatus ItemStore::update(ItemHandle handle, const ItemRecord& record, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    ItemRecord next = *stored.value;
    next.title = record.title;
    next.summary = record.summary;
    next.parent = record.parent;
    const KiottySpecStatus written = writeVersion(next, TagEdit{false, 0, 0}, nullptr, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    next.checkpoint = CheckpointHandle{sequence, 0};
    *stored.value = next;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemStore::applyTag(ItemHandle handle, ItemKind kind, uint32_t tag, uint64_t value)
{
    const size_t position = lowerBound(handle.index, tag);
    const bool exists = position < _tag_count && _tags[position].item.index == handle.index && _tags[position].tag == tag;
    if (exists && value != 0) {
        _tags[position].value = value;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    if (exists) {
        for (size_t index = position + 1; index < _tag_count; ++index) { _tags[index - 1] = _tags[index]; }
        --_tag_count;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    if (value == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (_tag_count == _tags.size()) { return KIOTTY_SPEC_STATUS_POOL_IS_FULL; }
    for (size_t index = _tag_count; index > position; --index) { _tags[index] = _tags[index - 1]; }
    _tags[position] = TagRecord{handle, kind, tag, value};
    ++_tag_count;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemStore::setTag(ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const bool adds = value != 0 && tagValue(handle, tag) == 0;
    if (adds && _tag_count == _tags.size()) {
        return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 에 닿음")});
    }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{true, tag, value}, nullptr, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return applyTag(handle, stored.value->kind, tag, value);
}

KiottySpecStatus ItemStore::writeBody(ItemHandle handle, Span<const char> body, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{false, 0, 0}, &body, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

void ItemStore::dropTags(ItemHandle handle)
{
    size_t kept = 0;
    for (size_t index = 0; index < _tag_count; ++index) {
        if (_tags[index].item.index == handle.index) { continue; }
        _tags[kept++] = _tags[index];
    }
    _tag_count = kept;
}

KiottySpecStatus ItemStore::remove(ItemHandle handle, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{false, 0, 0}, nullptr, true, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    dropTags(handle);
    _id_index.erase(stored.value->id);
    return _items.release(handle);
}

Result<size_t, KiottySpecStatus> ItemStore::readBody(ItemHandle handle, char* buffer, size_t capacity)
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
