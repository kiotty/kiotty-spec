#include "implementation/data/repository/kiotty_item_writer.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::TagRecord;

constexpr size_t MAX_TAG_TEXT_LENGTH = 8191;
constexpr size_t MAX_NUMBER_DIGITS = 20;
using TagText = FixedString<MAX_TAG_TEXT_LENGTH>;

constexpr char WRITE_SQL[] =
    "INSERT OR REPLACE INTO item_version (item_id, checkpoint, kind, parent_id, title, summary, body, tags, removed) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, COALESCE(?7, (SELECT body FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 "
    "ORDER BY checkpoint DESC LIMIT 1), ''), ?8, ?9)";

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

}

KiottySpecStatus ItemWriter::writeVersion(const ItemRecord& record, TagEdit edit, const Span<const char>* body, bool removed, uint32_t sequence)
{
    TagText tags;
    tags.assign(literalText("["));
    if (!removed) {
        const auto found = reader().find(record.id.view());
        const Span<const TagRecord> current = found ? this->tags().of(found.value) : Span<const TagRecord>();
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

Result<ItemHandle, KiottySpecStatus> ItemWriter::create(const ItemRecord& record, uint32_t sequence)
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

KiottySpecStatus ItemWriter::update(ItemHandle handle, const ItemRecord& record, uint32_t sequence)
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

KiottySpecStatus ItemWriter::applyTag(ItemHandle handle, ItemKind kind, uint32_t tag, uint64_t value)
{
    const size_t position = tags().position(handle.index, tag);
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

KiottySpecStatus ItemWriter::setTag(ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const bool adds = value != 0 && tags().value(handle, tag) == 0;
    if (adds && _tag_count == _tags.size()) {
        return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 에 닿음")});
    }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{true, tag, value}, nullptr, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return applyTag(handle, stored.value->kind, tag, value);
}

KiottySpecStatus ItemWriter::writeBody(ItemHandle handle, Span<const char> body, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{false, 0, 0}, &body, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

void ItemWriter::dropTags(ItemHandle handle)
{
    size_t kept = 0;
    for (size_t index = 0; index < _tag_count; ++index) {
        if (_tags[index].item.index == handle.index) { continue; }
        _tags[kept++] = _tags[index];
    }
    _tag_count = kept;
}

KiottySpecStatus ItemWriter::remove(ItemHandle handle, uint32_t sequence)
{
    const auto stored = _items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus written = writeVersion(*stored.value, TagEdit{false, 0, 0}, nullptr, true, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    dropTags(handle);
    _id_index.erase(stored.value->id);
    return _items.release(handle);
}

}
