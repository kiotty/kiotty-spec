#include "implementation/data/datasource/schema.h"

#include "implementation/core/error.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kSchemaV1 = R"SQL(
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

}

const std::vector<VersionedTable> &versionedTables() {
    static const std::vector<VersionedTable> tables{
        {"item_version", "item_id", true},
        {"link_version", "from_id, to_id, type", true},
        {"file_version", "file_id", true},
        {"decision_version", "kind", false},
        {"milestone_state", "milestone_id, item_id", false},
    };
    return tables;
}

const std::vector<std::string_view> &allTables() {
    static const std::vector<std::string_view> tables{
        "meta", "id_counter", "checkpoint", "item_version", "link_version", "file_version",
        "decision_version", "milestone_state", "change_request", "result",
    };
    return tables;
}

std::string_view primaryKeyOrder(std::string_view table) {
    if (table == "meta") return "key";
    if (table == "id_counter") return "prefix";
    if (table == "checkpoint") return "seq";
    if (table == "item_version") return "item_id, cp_seq";
    if (table == "link_version") return "from_id, to_id, type, cp_seq";
    if (table == "file_version") return "file_id, cp_seq";
    if (table == "decision_version") return "kind, cp_seq";
    if (table == "milestone_state") return "milestone_id, item_id, cp_seq";
    if (table == "change_request") return "id";
    if (table == "result") return "id";
    fail(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 테이블: " + std::string(table));
}

int32_t version(Database &db) {
    auto stmt = db.prepare("PRAGMA user_version");
    return stmt.step() ? static_cast<int32_t>(stmt.integer(0)) : 0;
}

void ensure(Database &db) {
    const int32_t current = version(db);
    if (current > kVersion) {
        fail(KIOTTY_SPEC_STATUS_DATABASE, "DB 스키마 버전 " + std::to_string(current) +
                                            " 이 라이브러리 버전 " + std::to_string(kVersion) + " 보다 높음");
    }
    db.exec(kSchemaV1);
    if (current < kVersion) {
        db.exec("PRAGMA user_version = " + std::to_string(kVersion));
    }
}

}
