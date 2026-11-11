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

struct WorkAreas {
    Span<char> buffer {};
    Span<char> line {};
};

WorkAreas split(TextBuffer& work);
bool isConflictMarker(std::string_view line);

}

KiottySpecStatus SpecFile::save()
{
    const KiottySpecStatus unchanged = checkUnchanged();
    if (unchanged != KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
    const WorkAreas areas = split(_work);
    KispFileWriter file {};
    KiottySpecStatus status = file.open(_database.view());
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                                _database.view());
    }
    status = SpecWriter(_t, file, areas.line).write();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = file.commit();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                                _database.view());
    }
    _hash = file.hash();
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecFile::checkUnchanged()
{
    const WorkAreas areas = split(_work);
    const auto current = hashKispFile(_database.view(), areas.buffer);
    if (!current) {
        return _t.failure.raise(current.code, KIOTTY_SPEC_SUBJECT_FILE, _database.view());
    }
    if (current.value == _hash) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_FILE_CHANGED,
                            _database.view());
}

KiottySpecStatus SpecFile::open(Span<const char> database)
{
    const KiottySpecStatus status = _database.assign(database);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(status, KIOTTY_SPEC_SUBJECT_FILE, database);
    }
    return load();
}

KiottySpecStatus SpecFile::load()
{
    clear();
    const WorkAreas areas = split(_work);
    KispFileReader reader(areas.buffer, areas.line);
    const KiottySpecStatus opened = reader.open(_database.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS && opened != KIOTTY_SPEC_STATUS_NOT_FOUND) {
        return _t.failure.raise(opened, KIOTTY_SPEC_SUBJECT_FILE, _database.view());
    }
    const KiottySpecStatus read = readLines(reader);
    if (read != KIOTTY_SPEC_STATUS_SUCCESS) { return read; }
    _hash = reader.hash();
    return finishLoad();
}

void SpecFile::clear()
{
    _t.meta.clear();
    _t.checkpoints.clear();
    _t.items.clear();
    _t.relations.versions().clear();
    _t.decisions.versions().clear();
}

KiottySpecStatus SpecFile::readLines(KispFileReader& reader)
{
    Result<bool, KiottySpecStatus> next = reader.next();
    for (; next && next.value; next = reader.next()) {
        const KiottySpecStatus status = readLine(reader);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
            return lineFailure(reader.lineNumber(), status);
        }
    }
    if (next) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _t.failure.raise(next.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                            literalText("work_buffer_bytes"), reader.lineNumber() + 1);
}

KiottySpecStatus SpecFile::readLine(KispFileReader& reader)
{
    const std::string_view line = viewOf(reader.line());
    const size_t line_number = reader.lineNumber();
    if (line_number == 1 && line != KISP_HEADER) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                KIOTTY_SPEC_SUBJECT_FILE_FORMAT, _database.view(),
                                reader.lineNumber());
    }
    const bool is_conflict_marker = isConflictMarker(line);
    if (is_conflict_marker) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_MERGE_MARKER,
                                _database.view(), reader.lineNumber());
    }
    const bool is_empty = line.empty();
    if (is_empty) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const char first = line.front();
    if (first == COMMENT) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto fields = reader.fields();
    if (!fields) {
        return _t.failure.raise(fields.code, KIOTTY_SPEC_SUBJECT_FILE_LINE,
                                spanOf(line.substr(0, line.find(FIELD_SEPARATOR))));
    }
    return SpecParser(_t).parse(fields.value);
}

KiottySpecStatus SpecFile::lineFailure(size_t line_number, KiottySpecStatus status)
{ return _t.failure.locate(status, line_number); }

KiottySpecStatus SpecFile::finishLoad()
{
    ItemTables& items = _t.items.tables();
    items.versions.sort();
    items.tag_versions.sort();
    _t.relations.versions().sort();
    const bool duplicated = items.versions.findDuplicate() || items.tag_versions.findDuplicate() ||
                            _t.relations.versions().findDuplicate();
    if (duplicated) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE);
    }
    const KiottySpecStatus rebuilt = _t.items.rebuild();
    if (rebuilt != KIOTTY_SPEC_STATUS_SUCCESS) { return rebuilt; }
    return _t.relations.rebuild();
}

namespace {

WorkAreas split(TextBuffer& work)
{
    const Span<char> all = work.scratch();
    return WorkAreas { Span<char>(all.data(), READ_BUFFER_BYTES),
                       Span<char>(all.data() + READ_BUFFER_BYTES, LINE_BYTES) };
}

bool isConflictMarker(std::string_view line)
{
    for (const std::string_view marker : CONFLICT_MARKERS) {
        const std::string_view prefix = line.substr(0, marker.size());
        if (prefix == marker) { return true; }
    }
    return false;
}

}

}
