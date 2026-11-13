#include "implementation/data/repository/spec_file/kiotty_spec_file.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/spec_file/kiotty_rule_spec_writer.h"
#include "implementation/data/repository/spec_file/kiotty_spec_parser.h"
#include "implementation/data/repository/spec_file/kiotty_spec_writer.h"

#include <string_view>
#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t READ_BUFFER_BYTES = 64 * 1024;
constexpr size_t LINE_BYTES = 2 * entity::MAX_BODY_LENGTH + 1024;
static_assert(READ_BUFFER_BYTES + LINE_BYTES <= KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES,
              "work buffer minimum must hold the read buffer and one line");
constexpr std::string_view CONFLICT_MARKERS[] = {"<<<<<<<", "=======", ">>>>>>>"};
constexpr char COMMENT = '#';
constexpr uint32_t UNKNOWN_MODEL_VERSION = 0;
constexpr size_t HEADER_LINE_NUMBER = 1;
constexpr char FIELD_SEPARATOR = '\t';

using HashResult = Result<uint64_t, KiottySpecStatus>;

uint32_t readModelVersion(std::string_view header);
bool isConflictMarker(std::string_view line);
bool isSameHash(uint64_t current_hash, uint64_t saved_hash);

}

KiottySpecStatus SpecFile::save()
{
    const KiottySpecStatus unchanged = checkUnchanged();
    if (unchanged != KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
    OpenedFileResult opened = openTemporaryFile();
    if (!opened) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                                     _database.view());
    }
    return writeLines(std::move(opened.value));
}

KiottySpecStatus SpecFile::exportRules(Span<const char> path, uint32_t checkpoint)
{
    KiottySpecStatus status = _temporary_path.assign(path);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _temporary_path.append(literalText(KISP_TEMPORARY_SUFFIX));
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    OpenedFileResult opened = OpenedFile::openForWriting(_file_system, _temporary_path.view());
    if (!opened) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE, path);
    }
    const WorkAreas areas = splitWork();
    KispFileWriter file(_file_system, std::move(opened.value), path, _temporary_path.view());
    status = RuleSpecWriter(_tables, file, areas.line).write(checkpoint);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = file.commit();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE, path);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

OpenedFileResult SpecFile::openTemporaryFile()
{
    KiottySpecStatus status = _temporary_path.assign(_database.view());
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _temporary_path.append(literalText(KISP_TEMPORARY_SUFFIX));
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return OpenedFileResult::failure(status); }
    return OpenedFile::openForWriting(_file_system, _temporary_path.view());
}

KiottySpecStatus SpecFile::writeLines(OpenedFile temporary_file)
{
    const WorkAreas areas = splitWork();
    KispFileWriter file(_file_system, std::move(temporary_file), _database.view(),
                        _temporary_path.view());
    KiottySpecStatus status = SpecWriter(_tables, file, areas.line).write();
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
    _model_version = MODEL_VERSION;
    _tables.meta.clear();
    _tables.checkpoints.clear();
    _tables.items.maintenance().clear();
    _tables.relations.versions().batch().clear();
    _tables.decisions.versions().batch().clear();
}

Result<uint64_t, KiottySpecStatus> SpecFile::readLines()
{
    OpenedFileResult opened = openKispFile(_file_system, _database.view());
    const bool is_missing = opened.code == KIOTTY_SPEC_STATUS_NOT_FOUND;
    if (is_missing) { return HashResult::success(EMPTY_KISP_HASH); }
    if (!opened) {
        return HashResult::failure(
            _tables.failure.raise(opened.code, KIOTTY_SPEC_SUBJECT_FILE, _database.view()));
    }
    return readOpenedLines(std::move(opened.value));
}

Result<uint64_t, KiottySpecStatus> SpecFile::readOpenedLines(OpenedFile file)
{
    const WorkAreas areas = splitWork();
    KispFileReader reader(std::move(file), areas.buffer, areas.line);
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
    const bool is_header = line_number == HEADER_LINE_NUMBER;
    if (is_header) { return readHeader(line); }
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

KiottySpecStatus SpecFile::readHeader(std::string_view line)
{
    const uint32_t version = readModelVersion(line);
    _model_version = version;
    if (version == UNKNOWN_MODEL_VERSION) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                     KIOTTY_SPEC_SUBJECT_FILE_FORMAT, _database.view(),
                                     HEADER_LINE_NUMBER);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
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

uint32_t readModelVersion(std::string_view header)
{
    if (header == KISP_HEADER) { return MODEL_VERSION; }
    if (header == KISP_PREVIOUS_HEADER) { return PREVIOUS_MODEL_VERSION; }
    return UNKNOWN_MODEL_VERSION;
}

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
