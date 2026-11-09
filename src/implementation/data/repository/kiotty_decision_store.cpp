#include "implementation/data/repository/kiotty_decision_store.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_schema.h"

namespace kiotty::spec::implementation {

KiottySpecStatus DecisionStore::write(entity::ItemKind kind, Span<const char> text, uint32_t sequence)
{
    if (text.length() > MAX_DECISION_BYTES) {
        return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("결정 기록은 2048 바이트 이하 (R-7)")});
    }
    auto query = _storage.prepare(literalText("INSERT OR REPLACE INTO decision_version (kind, checkpoint, description) VALUES (?1, ?2, ?3)"));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(entity::kindName(kind)));
    query.value.bind(2, static_cast<int64_t>(sequence));
    query.value.bind(3, text);
    return _storage.check(query.value.run());
}

Result<size_t, KiottySpecStatus> DecisionStore::read(entity::ItemKind kind, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    auto query = _storage.prepare(literalText("SELECT description FROM decision_version WHERE kind = ?1 ORDER BY checkpoint DESC LIMIT 1"));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, spanOf(entity::kindName(kind)));
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_storage.check(stepped.code)); }
    const Span<const char> text = stepped.value ? query.value.row().text(0) : Span<const char>();
    if (buffer == nullptr || capacity <= text.length()) { return SizeResult{KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, text.length() + 1}; }
    for (size_t index = 0; index < text.length(); ++index) { buffer[index] = text[index]; }
    buffer[text.length()] = '\0';
    return SizeResult::success(text.length());
}

KiottySpecStatus pruneVersions(Storage& storage, uint32_t base)
{
    for (const VersionedTable& table : versionedTables()) {
        const std::string_view statements[] = {table.keep_latest_sql, table.delete_older_sql, table.delete_removed_sql};
        for (const std::string_view sql : statements) {
            if (sql.empty()) { continue; }
            auto query = storage.prepare(spanOf(sql));
            if (!query) { return query.code; }
            query.value.bind(1, static_cast<int64_t>(base));
            const KiottySpecStatus ran = storage.check(query.value.run());
            if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return ran; }
        }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
