#include "implementation/data/repository/file_store.h"

#include "implementation/core/text_view.h"
#include "implementation/data/repository/record_rows.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr int ID_WIDTH = 3;
constexpr char ID_PREFIX[] = "FILE";

constexpr char BY_ID_SQL[] =
    "SELECT v.file_id, v.cp_seq, v.role, v.path, v.hash FROM file_version v"
    " JOIN (SELECT file_id, MAX(cp_seq) AS m FROM file_version WHERE cp_seq <= ?1 GROUP BY file_id) l"
    " ON v.file_id = l.file_id AND v.cp_seq = l.m WHERE v.removed = 0 AND v.file_id = ?2";
constexpr char BY_PATH_SQL[] =
    "SELECT v.file_id, v.cp_seq, v.role, v.path, v.hash FROM file_version v"
    " JOIN (SELECT file_id, MAX(cp_seq) AS m FROM file_version WHERE cp_seq <= ?1 GROUP BY file_id) l"
    " ON v.file_id = l.file_id AND v.cp_seq = l.m WHERE v.removed = 0 AND v.path = ?2";
constexpr char LIST_SQL[] =
    "SELECT v.file_id, v.cp_seq, v.role, v.path, v.hash FROM file_version v"
    " JOIN (SELECT file_id, MAX(cp_seq) AS m FROM file_version WHERE cp_seq <= ?1 GROUP BY file_id) l"
    " ON v.file_id = l.file_id AND v.cp_seq = l.m WHERE v.removed = 0 AND (?2 = '' OR v.role = ?2) ORDER BY v.path";
constexpr char WRITE_SQL[] =
    "INSERT INTO file_version (file_id, cp_seq, role, path, hash, removed) VALUES (?1, ?2, ?3, ?4, ?5, 0)"
    " ON CONFLICT(file_id, cp_seq) DO UPDATE SET role = excluded.role, path = excluded.path,"
    " hash = excluded.hash, removed = 0";

}

TextResult FileStore::registerFile(KiottySpecFileRole role, std::string_view path)
{
    const auto normalized = _projectFiles.normalize(spanOf(path));
    if (!normalized) { return TextResult::failure(normalized.code); }
    const auto existing = first(BY_PATH_SQL, kCurrent, normalized.value.view());
    if (!existing) { return TextResult::failure(existing.code); }
    if (!existing.value) { return insert(role, normalized.value); }
    const FileRecord &file = *existing.value;
    if (file.role != role) { return TextResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 다른 역할로 등록된 파일: "), file.id.view()})); }
    const auto rescanned = rescan(file.id);
    if (!rescanned) { return TextResult::failure(rescanned.code); }
    return TextResult::success(std::string(file.id));
}

OptionalFileResult FileStore::find(std::string_view id, int64_t cut) { return first(BY_ID_SQL, cut, spanOf(id)); }

FileResult FileStore::get(std::string_view id, int64_t cut)
{
    auto found = find(id, cut);
    if (!found) { return FileResult::failure(found.code); }
    if (!found.value) { return FileResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("파일 없음: "), spanOf(id)})); }
    return FileResult::success(*found.value);
}

OptionalFileResult FileStore::byPath(std::string_view path, int64_t cut)
{
    const auto normalized = _projectFiles.normalize(spanOf(path));
    if (!normalized) { return OptionalFileResult::failure(normalized.code); }
    return first(BY_PATH_SQL, cut, normalized.value.view());
}

FlagResult FileStore::rescan(std::string_view id)
{
    auto record = get(id);
    if (!record) { return FlagResult::failure(record.code); }
    const HashText hash = _projectFiles.hashOf(record.value.path.view());
    if (hash == record.value.hash) { return FlagResult::success(false); }
    record.value.hash = hash;
    const KiottySpecStatus written = writeVersion(record.value);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return FlagResult::failure(written); }
    return FlagResult::success(true);
}

FileListResult FileStore::list(KiottySpecFileRole roleOr0, int64_t cut)
{
    auto query = _storage.prepare(literalText(LIST_SQL));
    if (!query) { return FileListResult::failure(query.code); }
    const std::string_view role = roleOr0 != 0 ? name(roleOr0) : std::string_view();
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(role));
    return collectFiles(_storage, query.value);
}

FileListResult FileStore::changedSince(int64_t cpSeq)
{
    auto files = list(static_cast<KiottySpecFileRole>(0));
    if (!files) { return files; }
    std::vector<FileRecord> result;
    for (const FileRecord &file : files.value) {
        if (file.cpSeq > cpSeq) { result.push_back(file); }
    }
    return FileListResult::success(std::move(result));
}

OptionalFileResult FileStore::first(std::string_view sql, int64_t cut, Span<const char> key)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return OptionalFileResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, key);
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalFileResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalFileResult::success(std::nullopt); }
    const auto record = readFileRow(_storage, query.value.row());
    if (!record) { return OptionalFileResult::failure(record.code); }
    return OptionalFileResult::success(record.value);
}

TextResult FileStore::insert(KiottySpecFileRole role, const PathText &normalized)
{
    auto id = _meta.nextId(ID_PREFIX, ID_WIDTH);
    if (!id) { return id; }
    FileRecord record;
    record.id.assign(spanOf(id.value));
    record.role = role;
    record.path = normalized;
    record.hash = _projectFiles.hashOf(normalized.view());
    const KiottySpecStatus written = writeVersion(record);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(written); }
    return id;
}

KiottySpecStatus FileStore::writeVersion(const FileRecord &record)
{
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, record.id.view());
    query.value.bind(2, kPending);
    query.value.bind(3, spanOf(name(record.role)));
    query.value.bind(4, record.path.view());
    query.value.bind(5, record.hash.view());
    return _storage.check(query.value.run());
}

}
