#include "implementation/data/datasource/schema.h"

#include "implementation/core/fixed_string.h"
#include "implementation/core/text_view.h"

#include <cstdint>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view SCHEMA_V1 = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS id_counter (
    prefix TEXT PRIMARY KEY,
    next   INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS checkpoint (
    seq        INTEGER PRIMARY KEY,
    step       INTEGER NOT NULL,
    confirmed  INTEGER NOT NULL DEFAULT 0,
    purpose    TEXT NOT NULL DEFAULT 'none',
    note       TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS item_version (
    item_id   TEXT NOT NULL,
    cp_seq    INTEGER NOT NULL,
    wseq      INTEGER NOT NULL,
    kind      TEXT NOT NULL,
    category  TEXT NOT NULL,
    parent_id TEXT,
    seq       INTEGER NOT NULL DEFAULT 0,
    title     TEXT NOT NULL,
    body      TEXT NOT NULL,
    removed   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (item_id, cp_seq)
);
CREATE TABLE IF NOT EXISTS link_version (
    from_id TEXT NOT NULL,
    to_id   TEXT NOT NULL,
    type    TEXT NOT NULL,
    cp_seq  INTEGER NOT NULL,
    removed INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (from_id, to_id, type, cp_seq)
);
CREATE INDEX IF NOT EXISTS link_version_to ON link_version (to_id);
CREATE TABLE IF NOT EXISTS file_version (
    file_id TEXT NOT NULL,
    cp_seq  INTEGER NOT NULL,
    role    TEXT NOT NULL,
    path    TEXT NOT NULL,
    hash    TEXT NOT NULL,
    removed INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (file_id, cp_seq)
);
CREATE TABLE IF NOT EXISTS decision_version (
    kind   TEXT NOT NULL,
    cp_seq INTEGER NOT NULL,
    body   TEXT NOT NULL,
    PRIMARY KEY (kind, cp_seq)
);
CREATE TABLE IF NOT EXISTS milestone_state (
    milestone_id       TEXT NOT NULL,
    item_id            TEXT NOT NULL,
    cp_seq             INTEGER NOT NULL,
    state              TEXT NOT NULL,
    implemented_wseq   INTEGER,
    implemented_cp_seq INTEGER,
    PRIMARY KEY (milestone_id, item_id, cp_seq)
);
CREATE TABLE IF NOT EXISTS change_request (
    id         TEXT PRIMARY KEY,
    source     TEXT NOT NULL,
    body       TEXT NOT NULL,
    entry_step INTEGER NOT NULL,
    state      TEXT NOT NULL,
    cp_opened  INTEGER NOT NULL,
    cp_closed  INTEGER,
    note       TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS result (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    target_id TEXT NOT NULL,
    result    TEXT NOT NULL,
    log       TEXT NOT NULL,
    cp_seq    INTEGER NOT NULL,
    run_at    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS result_target ON result (target_id);
)SQL";

constexpr size_t MAX_NUMBER_LENGTH = 20;

constexpr VersionedTable VERSIONED_TABLES[] = {
    {"item_version",
     "UPDATE item_version SET cp_seq = ?1 WHERE cp_seq = ?2",
     "SELECT COUNT(*) FROM item_version WHERE cp_seq = ?1",
     "UPDATE item_version SET cp_seq = ?1 WHERE cp_seq < ?1 AND (item_id, cp_seq) IN (SELECT item_id, MAX(cp_seq) FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id)",
     "DELETE FROM item_version WHERE cp_seq < ?1",
     "DELETE FROM item_version WHERE removed = 1 AND cp_seq <= ?1"},
    {"link_version",
     "UPDATE link_version SET cp_seq = ?1 WHERE cp_seq = ?2",
     "SELECT COUNT(*) FROM link_version WHERE cp_seq = ?1",
     "UPDATE link_version SET cp_seq = ?1 WHERE cp_seq < ?1 AND (from_id, to_id, type, cp_seq) IN (SELECT from_id, to_id, type, MAX(cp_seq) FROM link_version WHERE cp_seq <= ?1 GROUP BY from_id, to_id, type)",
     "DELETE FROM link_version WHERE cp_seq < ?1",
     "DELETE FROM link_version WHERE removed = 1 AND cp_seq <= ?1"},
    {"file_version",
     "UPDATE file_version SET cp_seq = ?1 WHERE cp_seq = ?2",
     "SELECT COUNT(*) FROM file_version WHERE cp_seq = ?1",
     "UPDATE file_version SET cp_seq = ?1 WHERE cp_seq < ?1 AND (file_id, cp_seq) IN (SELECT file_id, MAX(cp_seq) FROM file_version WHERE cp_seq <= ?1 GROUP BY file_id)",
     "DELETE FROM file_version WHERE cp_seq < ?1",
     "DELETE FROM file_version WHERE removed = 1 AND cp_seq <= ?1"},
    {"decision_version",
     "UPDATE decision_version SET cp_seq = ?1 WHERE cp_seq = ?2",
     "SELECT COUNT(*) FROM decision_version WHERE cp_seq = ?1",
     "UPDATE decision_version SET cp_seq = ?1 WHERE cp_seq < ?1 AND (kind, cp_seq) IN (SELECT kind, MAX(cp_seq) FROM decision_version WHERE cp_seq <= ?1 GROUP BY kind)",
     "DELETE FROM decision_version WHERE cp_seq < ?1",
     ""},
    {"milestone_state",
     "UPDATE milestone_state SET cp_seq = ?1 WHERE cp_seq = ?2",
     "SELECT COUNT(*) FROM milestone_state WHERE cp_seq = ?1",
     "UPDATE milestone_state SET cp_seq = ?1 WHERE cp_seq < ?1 AND (milestone_id, item_id, cp_seq) IN (SELECT milestone_id, item_id, MAX(cp_seq) FROM milestone_state WHERE cp_seq <= ?1 GROUP BY milestone_id, item_id)",
     "DELETE FROM milestone_state WHERE cp_seq < ?1",
     ""},
};

constexpr OrderedTable ORDERED_TABLES[] = {
    {"meta", "key"},
    {"id_counter", "prefix"},
    {"checkpoint", "seq"},
    {"item_version", "item_id, cp_seq"},
    {"link_version", "from_id, to_id, type, cp_seq"},
    {"file_version", "file_id, cp_seq"},
    {"decision_version", "kind, cp_seq"},
    {"milestone_state", "milestone_id, item_id, cp_seq"},
    {"change_request", "id"},
    {"result", "id"},
};

constexpr char USER_VERSION_SQL[] = "PRAGMA user_version";
constexpr char SET_USER_VERSION_SQL[] = "PRAGMA user_version = 1";
static_assert(SCHEMA_VERSION == 1, "SET_USER_VERSION_SQL must follow SCHEMA_VERSION");

KiottySpecStatus rejectNewer(Storage& storage, int32_t current);

void appendNumber(FixedString<MAX_NUMBER_LENGTH>& text, int32_t value)
{
    char digits[MAX_NUMBER_LENGTH] {};
    size_t count = 0;
    int64_t rest = value < 0 ? -static_cast<int64_t>(value) : value;
    if (value < 0) { text.append(literalText("-")); }
    do {
        digits[count] = static_cast<char>('0' + rest % 10);
        rest /= 10;
        ++count;
    } while (rest > 0);
    for (size_t index = count; index > 0; --index) { text.append(Span<const char>(&digits[index - 1], 1)); }
}

}

Span<const VersionedTable> versionedTables()
{
    return Span<const VersionedTable>(VERSIONED_TABLES, sizeof(VERSIONED_TABLES) / sizeof(VERSIONED_TABLES[0]));
}

Span<const OrderedTable> orderedTables()
{
    return Span<const OrderedTable>(ORDERED_TABLES, sizeof(ORDERED_TABLES) / sizeof(ORDERED_TABLES[0]));
}

Result<int32_t, KiottySpecStatus> version(Storage& storage)
{
    auto query = storage.prepare(literalText(USER_VERSION_SQL));
    if (!query) { return Result<int32_t, KiottySpecStatus>::failure(query.code); }
    const auto stepped = query.value.step();
    if (!stepped) { return Result<int32_t, KiottySpecStatus>::failure(storage.check(stepped.code)); }
    const int64_t current = query.value.row().integer(0);
    return Result<int32_t, KiottySpecStatus>::success(static_cast<int32_t>(current));
}

KiottySpecStatus ensure(Storage& storage)
{
    const auto current = version(storage);
    if (!current) { return current.code; }
    if (current.value > SCHEMA_VERSION) { return rejectNewer(storage, current.value); }
    const KiottySpecStatus created = storage.execute(spanOf(SCHEMA_V1));
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return created; }
    if (current.value == SCHEMA_VERSION) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return storage.execute(literalText(SET_USER_VERSION_SQL));
}

namespace {

KiottySpecStatus rejectNewer(Storage& storage, int32_t current)
{
    FixedString<MAX_NUMBER_LENGTH> found;
    FixedString<MAX_NUMBER_LENGTH> supported;
    appendNumber(found, current);
    appendNumber(supported, SCHEMA_VERSION);
    return storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("DB 스키마 버전 "), found.view(), literalText(" 이 라이브러리 버전 "), supported.view(), literalText(" 보다 높음")});
}

}

}
