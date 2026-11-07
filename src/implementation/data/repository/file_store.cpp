#include "implementation/data/repository/file_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr int ID_WIDTH = 3;
constexpr char ID_PREFIX[] = "FILE";
constexpr char MISSING_HASH[] = "missing";
constexpr char PARENT_PREFIX[] = "..";

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

FileRecord readFileRecord(const Row &row)
{
    FileRecord record;
    record.id = std::string(viewOf(row.text(0)));
    record.cpSeq = row.integer(1);
    parse(viewOf(row.text(2)), record.role);
    record.path = std::string(viewOf(row.text(3)));
    record.hash = std::string(viewOf(row.text(4)));
    return record;
}

std::filesystem::path relativeInside(const std::filesystem::path &path, const std::filesystem::path &root)
{
    if (!path.is_absolute()) { return path; }
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error) { return path; }
    if (relative.empty()) { return path; }
    const bool isOutside = pathToUtf8(relative).rfind(PARENT_PREFIX, 0) == 0;
    if (isOutside) { return path; }
    return relative;
}

}

TextResult FileStore::registerFile(KiottySpecFileRole role, std::string_view path)
{
    const auto normalized = normalize(path);
    if (!normalized) { return normalized; }
    const auto existing = byPath(normalized.value);
    if (!existing) { return TextResult::failure(existing.code); }
    if (!existing.value) { return insert(role, normalized.value); }
    const FileRecord &file = *existing.value;
    if (file.role != role) { return TextResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 다른 역할로 등록된 파일: "), spanOf(file.id)})); }
    const auto rescanned = rescan(file.id);
    if (!rescanned) { return TextResult::failure(rescanned.code); }
    return TextResult::success(file.id);
}

OptionalFileResult FileStore::find(std::string_view id, int64_t cut) { return first(BY_ID_SQL, cut, id); }

FileResult FileStore::get(std::string_view id, int64_t cut)
{
    auto found = find(id, cut);
    if (!found) { return FileResult::failure(found.code); }
    if (!found.value) { return FileResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("파일 없음: "), spanOf(id)})); }
    return FileResult::success(std::move(*found.value));
}

OptionalFileResult FileStore::byPath(std::string_view path, int64_t cut)
{
    const auto normalized = normalize(path);
    if (!normalized) { return OptionalFileResult::failure(normalized.code); }
    return first(BY_PATH_SQL, cut, normalized.value);
}

FlagResult FileStore::rescan(std::string_view id)
{
    auto record = get(id);
    if (!record) { return FlagResult::failure(record.code); }
    const std::string hash = hashOf(record.value.path);
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
    std::vector<FileRecord> records;
    auto stepped = query.value.step();
    while (stepped.value) {
        records.push_back(readFileRecord(query.value.row()));
        stepped = query.value.step();
    }
    if (!stepped) { return FileListResult::failure(_storage.check(stepped.code)); }
    return FileListResult::success(std::move(records));
}

FileListResult FileStore::changedSince(int64_t cpSeq)
{
    auto files = list(static_cast<KiottySpecFileRole>(0));
    if (!files) { return files; }
    std::vector<FileRecord> result;
    for (FileRecord &file : files.value) {
        if (file.cpSeq > cpSeq) { result.push_back(std::move(file)); }
    }
    return FileListResult::success(std::move(result));
}

TextResult FileStore::normalize(std::string_view path) const
{
    const std::string_view trimmed = trim(path);
    if (trimmed.empty()) { return TextResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("경로가 비어 있음")})); }
    const std::filesystem::path inside = relativeInside(pathFromUtf8(trimmed), _root);
    return TextResult::success(pathToUtf8(inside.lexically_normal()));
}

std::filesystem::path FileStore::absolute(const FileRecord &file) const
{
    const std::filesystem::path path = pathFromUtf8(file.path);
    if (path.is_absolute()) { return path; }
    return _root / path;
}

std::string FileStore::hashOf(std::string_view normalizedPath) const
{
    FileRecord probe;
    probe.path = std::string(normalizedPath);
    const auto content = readFile(absolute(probe));
    if (!content) { return MISSING_HASH; }
    return fnv1a64Hex(*content);
}

std::optional<std::string> FileStore::read(const FileRecord &file) const { return readFile(absolute(file)); }

OptionalFileResult FileStore::first(std::string_view sql, int64_t cut, std::string_view key)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return OptionalFileResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(key));
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalFileResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalFileResult::success(std::nullopt); }
    return OptionalFileResult::success(readFileRecord(query.value.row()));
}

TextResult FileStore::insert(KiottySpecFileRole role, const std::string &normalized)
{
    auto id = _meta.nextId(ID_PREFIX, ID_WIDTH);
    if (!id) { return id; }
    FileRecord record;
    record.id = id.value;
    record.role = role;
    record.path = normalized;
    record.hash = hashOf(normalized);
    const KiottySpecStatus written = writeVersion(record);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(written); }
    return id;
}

KiottySpecStatus FileStore::writeVersion(const FileRecord &record)
{
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(record.id));
    query.value.bind(2, kPending);
    query.value.bind(3, spanOf(name(record.role)));
    query.value.bind(4, spanOf(record.path));
    query.value.bind(5, spanOf(record.hash));
    return _storage.check(query.value.run());
}

}
