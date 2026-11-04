#include "implementation/base/file_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

namespace kiotty::base {

using core::FileRecord;

namespace {

constexpr std::string_view kSelect =
    "SELECT v.file_id, v.cp_seq, v.role, v.path, v.hash FROM file_version v"
    " JOIN (SELECT file_id, MAX(cp_seq) AS m FROM file_version WHERE cp_seq <= ?1 GROUP BY file_id) l"
    " ON v.file_id = l.file_id AND v.cp_seq = l.m WHERE v.removed = 0 ";

FileRecord readFileRecord(const core::Statement &stmt) {
    FileRecord record;
    record.id = stmt.text(0);
    record.cpSeq = stmt.integer(1);
    core::parse(stmt.text(2), record.role);
    record.path = stmt.text(3);
    record.hash = stmt.text(4);
    return record;
}

} // namespace

std::string FileStore::normalize(std::string_view path) const {
    core::require(!core::trim(path).empty(), KiottySpecStatus_InvalidArg, "경로가 비어 있음");
    std::filesystem::path p = core::pathFromUtf8(core::trim(path));
    if (p.is_absolute()) {
        std::error_code ec;
        const auto relative = std::filesystem::relative(p, root_, ec);
        if (!ec && !relative.empty() && core::pathToUtf8(relative).rfind("..", 0) != 0) {
            p = relative;
        }
    }
    return core::pathToUtf8(p.lexically_normal());
}

std::filesystem::path FileStore::absolute(const FileRecord &file) const {
    const auto p = core::pathFromUtf8(file.path);
    return p.is_absolute() ? p : root_ / p;
}

std::string FileStore::hashOf(std::string_view normalizedPath) const {
    FileRecord probe;
    probe.path = std::string(normalizedPath);
    const auto content = core::readFile(absolute(probe));
    return content ? core::fnv1a64Hex(*content) : std::string("missing");
}

std::optional<std::string> FileStore::read(const FileRecord &file) const { return core::readFile(absolute(file)); }

std::string FileStore::registerFile(KiottySpecFileRole role, std::string_view path) {
    const std::string normalized = normalize(path);
    if (auto existing = byPath(normalized)) {
        core::require(existing->role == role, KiottySpecStatus_Conflict,
                      "이미 다른 역할로 등록된 파일: " + existing->id);
        rescan(existing->id);
        return existing->id;
    }
    FileRecord record;
    record.id = meta_.nextId("FILE", 3);
    record.role = role;
    record.path = normalized;
    record.hash = hashOf(normalized);
    writeVersion(record);
    return record.id;
}

std::optional<FileRecord> FileStore::find(std::string_view id, int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "AND v.file_id = ?2");
    stmt.bindAll(cut, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readFileRecord(stmt);
}

FileRecord FileStore::get(std::string_view id, int64_t cut) {
    auto record = find(id, cut);
    core::require(record.has_value(), KiottySpecStatus_NotFound, "파일 없음: " + std::string(id));
    return *record;
}

std::optional<FileRecord> FileStore::byPath(std::string_view path, int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "AND v.path = ?2");
    stmt.bindAll(cut, normalize(path));
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readFileRecord(stmt);
}

bool FileStore::rescan(std::string_view id) {
    auto record = get(id);
    const std::string hash = hashOf(record.path);
    if (hash == record.hash) {
        return false;
    }
    record.hash = hash;
    writeVersion(record);
    return true;
}

std::vector<FileRecord> FileStore::list(KiottySpecFileRole roleOr0, int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "AND (?2 = '' OR v.role = ?2) ORDER BY v.path");
    stmt.bindAll(cut, roleOr0 != 0 ? std::string(core::name(roleOr0)) : std::string());
    std::vector<FileRecord> records;
    while (stmt.step()) {
        records.push_back(readFileRecord(stmt));
    }
    return records;
}

std::vector<FileRecord> FileStore::changedSince(int64_t cpSeq) {
    std::vector<FileRecord> result;
    for (auto &file : list(static_cast<KiottySpecFileRole>(0))) {
        if (file.cpSeq > cpSeq) {
            result.push_back(std::move(file));
        }
    }
    return result;
}

void FileStore::writeVersion(const FileRecord &record) {
    db_.prepare(
           "INSERT INTO file_version (file_id, cp_seq, role, path, hash, removed) VALUES (?1, ?2, ?3, ?4, ?5, 0)"
           " ON CONFLICT(file_id, cp_seq) DO UPDATE SET role = excluded.role, path = excluded.path,"
           " hash = excluded.hash, removed = 0")
        .bindAll(record.id, core::kPending, core::name(record.role), record.path, record.hash)
        .run();
}

} // namespace kiotty::base
