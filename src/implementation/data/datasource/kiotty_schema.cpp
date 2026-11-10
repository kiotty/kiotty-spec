#include "implementation/data/datasource/kiotty_schema.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_savepoint.h"

#include <cstdint>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view SCHEMA_V3 = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS id_counter (
    prefix TEXT PRIMARY KEY,
    next   INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS checkpoint (
    sequence    INTEGER PRIMARY KEY,
    created_at  INTEGER NOT NULL,
    previous    INTEGER NOT NULL DEFAULT 0,
    step        INTEGER NOT NULL,
    state       TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS item (
    key INTEGER PRIMARY KEY,
    id  TEXT NOT NULL UNIQUE
);
CREATE TABLE IF NOT EXISTS item_version (
    item_key   INTEGER NOT NULL,
    checkpoint INTEGER NOT NULL,
    kind       TEXT NOT NULL,
    parent_key INTEGER,
    title      TEXT NOT NULL,
    summary    TEXT NOT NULL DEFAULT '',
    body       TEXT NOT NULL DEFAULT '',
    tags       TEXT NOT NULL DEFAULT '[]',
    removed    INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (item_key, checkpoint)
);
CREATE TABLE IF NOT EXISTS relation_version (
    from_key   INTEGER NOT NULL,
    model      TEXT NOT NULL,
    relation   INTEGER NOT NULL,
    to_key     INTEGER NOT NULL,
    checkpoint INTEGER NOT NULL,
    removed    INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (from_key, relation, to_key, checkpoint)
);
CREATE INDEX IF NOT EXISTS relation_version_to ON relation_version (to_key);
CREATE TABLE IF NOT EXISTS decision_version (
    kind        TEXT NOT NULL,
    checkpoint  INTEGER NOT NULL,
    description TEXT NOT NULL,
    PRIMARY KEY (kind, checkpoint)
);
)SQL";

// 스키마 2 의 버전 테이블. 옛 형식(스키마 1 · 2)의 jsonl 은 이 모양으로 넣은 뒤 MIGRATE_V2_SQL 로 바꾼다.
constexpr std::string_view SCHEMA_V2_ITEMS = R"SQL(
CREATE TABLE item_version (
    item_id TEXT NOT NULL, checkpoint INTEGER NOT NULL, kind TEXT NOT NULL, parent_id TEXT, title TEXT NOT NULL,
    summary TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', tags TEXT NOT NULL DEFAULT '[]',
    removed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (item_id, checkpoint)
);
CREATE TABLE relation_version (
    from_id TEXT NOT NULL, model TEXT NOT NULL, relation INTEGER NOT NULL, to_id TEXT NOT NULL,
    checkpoint INTEGER NOT NULL, removed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (from_id, relation, to_id, checkpoint)
);
)SQL";

// 스키마 2 → 3. 항목은 바뀌지 않는 key 로 서로를 가리키고, ID 는 item 표에만 있다. 그래서 rename 은 한 행만 고친다.
constexpr std::string_view MIGRATE_V2_SQL = R"SQL(
DROP INDEX IF EXISTS relation_version_to;
ALTER TABLE item_version RENAME TO item_version_v2;
ALTER TABLE relation_version RENAME TO relation_version_v2;
CREATE TABLE item (key INTEGER PRIMARY KEY, id TEXT NOT NULL UNIQUE);
INSERT INTO item (id) SELECT id FROM (SELECT item_id AS id FROM item_version_v2 UNION SELECT parent_id FROM item_version_v2
    WHERE parent_id IS NOT NULL UNION SELECT from_id FROM relation_version_v2 UNION SELECT to_id FROM relation_version_v2) ORDER BY id;
CREATE TABLE item_version (
    item_key INTEGER NOT NULL, checkpoint INTEGER NOT NULL, kind TEXT NOT NULL, parent_key INTEGER, title TEXT NOT NULL,
    summary TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', tags TEXT NOT NULL DEFAULT '[]',
    removed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (item_key, checkpoint)
);
INSERT INTO item_version SELECT i.key, v.checkpoint, v.kind, p.key, v.title, v.summary, v.body, v.tags, v.removed
    FROM item_version_v2 v JOIN item i ON i.id = v.item_id LEFT JOIN item p ON p.id = v.parent_id;
CREATE TABLE relation_version (
    from_key INTEGER NOT NULL, model TEXT NOT NULL, relation INTEGER NOT NULL, to_key INTEGER NOT NULL,
    checkpoint INTEGER NOT NULL, removed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (from_key, relation, to_key, checkpoint)
);
INSERT INTO relation_version SELECT f.key, v.model, v.relation, t.key, v.checkpoint, v.removed
    FROM relation_version_v2 v JOIN item f ON f.id = v.from_id JOIN item t ON t.id = v.to_id;
CREATE INDEX relation_version_to ON relation_version (to_key);
DROP TABLE item_version_v2;
DROP TABLE relation_version_v2;
PRAGMA user_version = 3;
)SQL";

constexpr char DROP_ALL_SQL[] =
    "DROP TABLE IF EXISTS meta; DROP TABLE IF EXISTS id_counter; DROP TABLE IF EXISTS checkpoint; DROP TABLE IF EXISTS item;"
    "DROP TABLE IF EXISTS item_version; DROP TABLE IF EXISTS relation_version; DROP TABLE IF EXISTS decision_version;"
    "DROP TABLE IF EXISTS link_version; DROP TABLE IF EXISTS file_version; DROP TABLE IF EXISTS milestone_state;"
    "DROP TABLE IF EXISTS change_request; DROP TABLE IF EXISTS result;";

// ?1 = 기준 체크포인트. 기준 이하의 가장 최근 버전을 기준으로 올리고, 그보다 오래된 버전을 지우고,
// 기준 시점에 지워져 있던 것은 흔적까지 지운다 (5.3 정리).
constexpr VersionedTable VERSIONED_TABLES[] = {
    {"item_version",
     "UPDATE item_version SET checkpoint = ?1 WHERE checkpoint < ?1 AND (item_key, checkpoint) IN "
     "(SELECT item_key, MAX(checkpoint) FROM item_version WHERE checkpoint <= ?1 GROUP BY item_key)",
     "DELETE FROM item_version WHERE checkpoint < ?1",
     "DELETE FROM item_version WHERE removed = 1 AND checkpoint <= ?1"},
    {"relation_version",
     "UPDATE relation_version SET checkpoint = ?1 WHERE checkpoint < ?1 AND (from_key, relation, to_key, checkpoint) IN "
     "(SELECT from_key, relation, to_key, MAX(checkpoint) FROM relation_version WHERE checkpoint <= ?1 GROUP BY from_key, relation, to_key)",
     "DELETE FROM relation_version WHERE checkpoint < ?1",
     "DELETE FROM relation_version WHERE removed = 1 AND checkpoint <= ?1"},
    {"decision_version",
     "UPDATE decision_version SET checkpoint = ?1 WHERE checkpoint < ?1 AND (kind, checkpoint) IN "
     "(SELECT kind, MAX(checkpoint) FROM decision_version WHERE checkpoint <= ?1 GROUP BY kind)",
     "DELETE FROM decision_version WHERE checkpoint < ?1",
     ""},
    // 버전이 모두 지워진 항목의 ID 는 정리 뒤에 다시 쓸 수 있다.
    {"item", "", "", "DELETE FROM item WHERE ?1 IS NOT NULL AND key NOT IN (SELECT item_key FROM item_version)"},
};

constexpr OrderedTable ORDERED_TABLES[] = {
    {"meta", "key"},
    {"id_counter", "prefix"},
    {"checkpoint", "sequence"},
    {"item", "key"},
    {"item_version", "item_key, checkpoint"},
    {"relation_version", "from_key, relation, to_key, checkpoint"},
    {"decision_version", "kind, checkpoint"},
};

constexpr char USER_VERSION_SQL[] = "PRAGMA user_version";
constexpr char SET_USER_VERSION_SQL[] = "PRAGMA user_version = 3";
static_assert(SCHEMA_VERSION == 3, "SET_USER_VERSION_SQL must follow SCHEMA_VERSION");

Result<int32_t, KiottySpecStatus> readVersion(Storage& storage)
{
    auto query = storage.prepare(literalText(USER_VERSION_SQL));
    if (!query) { return Result<int32_t, KiottySpecStatus>::failure(query.code); }
    const auto stepped = query.value.step();
    if (!stepped) { return Result<int32_t, KiottySpecStatus>::failure(storage.check(stepped.code)); }
    return Result<int32_t, KiottySpecStatus>::success(static_cast<int32_t>(query.value.row().integer(0)));
}

bool hasTable(Storage& storage, Span<const char> name)
{
    auto query = storage.prepare(literalText("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1"));
    if (!query) { return false; }
    query.value.bind(1, name);
    const auto stepped = query.value.step();
    return stepped && stepped.value;
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

Result<int32_t, KiottySpecStatus> ensure(Storage& storage)
{
    const auto current = readVersion(storage);
    if (!current) { return current; }
    if (current.value > SCHEMA_VERSION) {
        return Result<int32_t, KiottySpecStatus>::failure(
            storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("DB 스키마가 이 라이브러리보다 새로움")}));
    }
    // 스키마 1 은 user_version 을 1 로 두었다. 그 전의 DB 는 0 이지만 테이블이 있다.
    if (current.value == LEGACY_SCHEMA_VERSION || (current.value == 0 && hasTable(storage, literalText("link_version")))) {
        return Result<int32_t, KiottySpecStatus>::success(LEGACY_SCHEMA_VERSION);
    }
    if (current.value == ID_KEYED_SCHEMA_VERSION) {
        const KiottySpecStatus migrated = migrateItemKeys(storage);
        if (migrated != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<int32_t, KiottySpecStatus>::failure(migrated); }
    }
    const KiottySpecStatus created = storage.executeScript(spanOf(SCHEMA_V3));
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<int32_t, KiottySpecStatus>::failure(created); }
    if (current.value != SCHEMA_VERSION) {
        const KiottySpecStatus marked = storage.executeScript(literalText(SET_USER_VERSION_SQL));
        if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<int32_t, KiottySpecStatus>::failure(marked); }
    }
    return Result<int32_t, KiottySpecStatus>::success(SCHEMA_VERSION);
}

KiottySpecStatus recreate(Storage& storage)
{
    const KiottySpecStatus dropped = storage.executeScript(literalText(DROP_ALL_SQL));
    if (dropped != KIOTTY_SPEC_STATUS_SUCCESS) { return dropped; }
    const KiottySpecStatus created = storage.executeScript(spanOf(SCHEMA_V3));
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return created; }
    return storage.executeScript(literalText(SET_USER_VERSION_SQL));
}

KiottySpecStatus recreateWithIdKeys(Storage& storage)
{
    KiottySpecStatus status = recreate(storage);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = storage.executeScript(literalText("DROP TABLE item; DROP TABLE item_version; DROP TABLE relation_version;"));
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = storage.executeScript(spanOf(SCHEMA_V2_ITEMS)); }
    return status;
}

// 한 savepoint 안에서 바꾼다. 실패하면 스키마 2 가 그대로 남는다.
KiottySpecStatus migrateItemKeys(Storage& storage)
{
    Savepoint savepoint(storage.connection());
    KiottySpecStatus status = storage.check(savepoint.begin());
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = storage.executeScript(spanOf(MIGRATE_V2_SQL)); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = storage.check(savepoint.commit()); }
    return status;
}

}
