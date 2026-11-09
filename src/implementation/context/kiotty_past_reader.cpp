#include "implementation/context/kiotty_past_reader.h"

#include "implementation/core/kiotty_text_view.h"

#include <cstring>
#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr char ITEM_AT_SQL[] =
    "SELECT checkpoint, kind, parent_id, title, summary, tags, removed FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 "
    "ORDER BY checkpoint DESC LIMIT 1";

constexpr char BODY_AT_SQL[] =
    "SELECT body, removed FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 ORDER BY checkpoint DESC LIMIT 1";

constexpr char DECISION_AT_SQL[] =
    "SELECT description FROM decision_version WHERE kind = ?1 AND checkpoint <= ?2 ORDER BY checkpoint DESC LIMIT 1";

KiottySpecStatus copyOut(Span<const char> text, char* buffer, size_t capacity, size_t& written)
{
    written = text.length();
    if (buffer == nullptr || capacity <= text.length()) {
        written = text.length() + 1;
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    std::memcpy(buffer, text.data(), text.length());
    buffer[text.length()] = '\0';
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

Result<ItemVersion, KiottySpecStatus> PastReader::itemAt(Span<const char> id, uint32_t checkpoint)
{
    using VersionResult = Result<ItemVersion, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(ITEM_AT_SQL));
    if (!query) { return VersionResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return VersionResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(6) != 0) {
        return VersionResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    const Row row = query.value.row();
    ItemVersion version;
    version.record.checkpoint = entity::CheckpointHandle{static_cast<uint32_t>(row.integer(0)), 0};
    version.record.kind = static_cast<ItemKind>(entity::parseKind(viewOf(row.text(1))));
    version.record.parent = entity::INVALID_ITEM_HANDLE;
    version.record.id.assign(id);
    version.record.title.assign(row.text(3));
    version.record.summary.assign(row.text(4));
    if (!row.isNull(2)) {
        version.parent_id.assign(row.text(2));
        const auto parent = _w.items.reader().find(row.text(2));
        if (parent) { version.record.parent = parent.value; }
    }
    return VersionResult::success(version);
}

Result<uint64_t, KiottySpecStatus> PastReader::tagAt(Span<const char> id, uint32_t tag, uint32_t checkpoint)
{
    using TagResult = Result<uint64_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(ITEM_AT_SQL));
    if (!query) { return TagResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return TagResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(6) != 0) {
        return TagResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    // "[[1,4],[2,1]]" 에서 tag 의 값을 찾는다.
    const std::string_view text = viewOf(query.value.row().text(5));
    uint64_t numbers[2] {};
    size_t count = 0;
    uint64_t current = 0;
    bool digits = false;
    for (const char letter : text) {
        if (letter >= '0' && letter <= '9') {
            current = current * 10 + static_cast<uint64_t>(letter - '0');
            digits = true;
            continue;
        }
        if (digits && count < 2) { numbers[count++] = current; }
        current = 0;
        digits = false;
        if (letter == ']' && count == 2) {
            if (numbers[0] == tag) { return TagResult::success(numbers[1]); }
            count = 0;
        }
    }
    return TagResult::success(0);
}

Result<size_t, KiottySpecStatus> PastReader::bodyAt(Span<const char> id, uint32_t checkpoint, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(BODY_AT_SQL));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(1) != 0) {
        return SizeResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    size_t written = 0;
    const KiottySpecStatus copied = copyOut(query.value.row().text(0), buffer, capacity, written);
    return SizeResult{copied, written};
}

Result<size_t, KiottySpecStatus> PastReader::decisionAt(ItemKind kind, uint32_t checkpoint, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(DECISION_AT_SQL));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, spanOf(entity::kindName(kind)));
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_w.storage.check(stepped.code)); }
    const Span<const char> text = stepped.value ? query.value.row().text(0) : Span<const char>();
    size_t written = 0;
    const KiottySpecStatus copied = copyOut(text, buffer, capacity, written);
    return SizeResult{copied, written};
}

}
