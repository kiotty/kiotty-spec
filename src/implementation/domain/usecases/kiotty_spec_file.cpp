#include "implementation/domain/usecases/kiotty_spec_file.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/kiotty_spec_parser.h"
#include "implementation/domain/usecases/kiotty_spec_writer.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t READ_BUFFER_BYTES = 64 * 1024;
constexpr size_t LINE_BYTES = 2 * entity::MAX_BODY_LENGTH + 1024;
static_assert(READ_BUFFER_BYTES + LINE_BYTES <= KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES,
              "work buffer minimum must hold the read buffer and one line");
constexpr std::string_view CONFLICT_MARKERS[] = {"<<<<<<<", "=======", ">>>>>>>"};
constexpr char COMMENT = '#';
constexpr char FIELD_SEPARATOR = '\t';

using HashResult = Result<uint64_t, KiottySpecStatus>;

bool isConflictMarker(std::string_view line);
bool isSameHash(uint64_t current_hash, uint64_t saved_hash);

}

KiottySpecStatus SpecFile::save()
{
    const KiottySpecStatus unchanged = checkUnchanged();
    if (unchanged != KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
    const WorkAreas areas = splitWork();
    KispFileWriter file(_file_system);
    KiottySpecStatus status = file.open(_database.view());
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                                     _database.view());
    }
    status = SpecWriter(_tables, file, areas.line).write();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = file.commit();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                                     _database.view());
    }
    _hash = file.hash();
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecFile::checkUnchanged()
{
    const WorkAreas areas = splitWork();
    const HashResult current = hashKispFile(_file_system, _database.view(), areas.buffer);
    if (!current) {
        return _tables.failure.raise(current.code, KIOTTY_SPEC_SUBJECT_FILE, _database.view());
    }
    const bool is_unchanged = isSameHash(current.value, _hash);
    if (is_unchanged) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _tables.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_FILE_CHANGED,
                                 _database.view());
}

KiottySpecStatus SpecFile::open(Span<const char> database)
{
    const KiottySpecStatus status = _database.assign(database);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(status, KIOTTY_SPEC_SUBJECT_FILE, database);
    }
    return load();
}

KiottySpecStatus SpecFile::load()
{
    clear();
    const HashResult read = readLines();
    if (!read) { return read.code; }
    _hash = read.value;
    return finishLoad();
}

void SpecFile::clear()
{
    _tables.meta.clear();
    _tables.checkpoints.clear();
    _tables.items.maintenance().clear();
    _tables.relations.versions().batch().clear();
    _tables.decisions.versions().batch().clear();
}

Result<uint64_t, KiottySpecStatus> SpecFile::readLines()
{
    const WorkAreas areas = splitWork();
    KispFileReader reader(_file_system, areas.buffer, areas.line);
    const KiottySpecStatus opened = reader.open(_database.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS && opened != KIOTTY_SPEC_STATUS_NOT_FOUND) {
        return HashResult::failure(
            _tables.failure.raise(opened, KIOTTY_SPEC_SUBJECT_FILE, _database.view()));
    }
    Result<bool, KiottySpecStatus> next = reader.next();
    for (; next && next.value; next = reader.next()) {
        const KiottySpecStatus status = readLine(reader);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
            return HashResult::failure(lineFailure(reader.lineNumber(), status));
        }
    }
    if (next) { return HashResult::success(reader.hash()); }
    return HashResult::failure(
        _tables.failure.raise(next.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("work_buffer_bytes"), reader.lineNumber() + 1));
}

KiottySpecStatus SpecFile::readLine(const KispFileReader& reader)
{
    const std::string_view line = viewOf(reader.line());
    const size_t line_number = reader.lineNumber();
    if (line_number == 1 && line != KISP_HEADER) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                     KIOTTY_SPEC_SUBJECT_FILE_FORMAT, _database.view(),
                                     reader.lineNumber());
    }
    const bool is_conflict_marker = isConflictMarker(line);
    if (is_conflict_marker) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_MERGE_MARKER,
                                     _database.view(), reader.lineNumber());
    }
    const bool is_empty = line.empty();
    if (is_empty) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const char first = line.front();
    if (first == COMMENT) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto fields = reader.fields();
    if (!fields) {
        return _tables.failure.raise(fields.code, KIOTTY_SPEC_SUBJECT_FILE_LINE,
                                     spanOf(line.substr(0, line.find(FIELD_SEPARATOR))));
    }
    return SpecParser(_tables).parse(fields.value);
}

KiottySpecStatus SpecFile::lineFailure(size_t line_number, KiottySpecStatus status)
{ return _tables.failure.locate(status, line_number); }

KiottySpecStatus SpecFile::finishLoad()
{
    ItemTables& items = _tables.items.tables();
    items.versions.batch().sort();
    items.tag_versions.batch().sort();
    _tables.relations.versions().batch().sort();
    const bool has_duplicate = items.versions.batch().findDuplicate() ||
                               items.tag_versions.batch().findDuplicate() ||
                               _tables.relations.versions().batch().findDuplicate();
    if (has_duplicate) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT,
                                     KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE);
    }
    const KiottySpecStatus rebuilt = _tables.items.maintenance().rebuild();
    if (rebuilt != KIOTTY_SPEC_STATUS_SUCCESS) { return rebuilt; }
    return _tables.relations.rebuild();
}

WorkAreas SpecFile::splitWork()
{
    const Span<char> all = _work.scratch();
    const Result<Span<char>, KiottySpecStatus> read_area = all.subspan(0, READ_BUFFER_BYTES);
    const Result<Span<char>, KiottySpecStatus> line_area =
        all.subspan(READ_BUFFER_BYTES, LINE_BYTES);
    return WorkAreas {read_area.value, line_area.value};
}

namespace {

bool isConflictMarker(std::string_view line)
{
    for (const std::string_view marker : CONFLICT_MARKERS) {
        const std::string_view prefix = line.substr(0, marker.size());
        if (prefix == marker) { return true; }
    }
    return false;
}

bool isSameHash(uint64_t current_hash, uint64_t saved_hash) { return current_hash == saved_hash; }

}

}
