#include "implementation/domain/usecases/kiotty_spec_file.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/kiotty_spec_parser.h"
#include "implementation/domain/usecases/kiotty_spec_writer.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

// 작업 버퍼를 줄 버퍼와 줄 영역으로 나눈다. 줄 영역은 모든 글자를 이스케이프한 가장 긴 본문 줄이 들어가는 크기다.
constexpr size_t READ_BUFFER_BYTES = 64 * 1024;
constexpr size_t LINE_BYTES = 2 * entity::MAX_BODY_LENGTH + 1024;
constexpr size_t MAX_LINE_MESSAGE = 512;
constexpr std::string_view CONFLICT_MARKERS[] = {"<<<<<<<", "=======", ">>>>>>>"};
constexpr char COMMENT = '#';

struct WorkAreas {
    Span<char> buffer;
    Span<char> line;
};

WorkAreas split(TextBuffer& work)
{
    const Span<char> all = work.scratch();
    return WorkAreas{Span<char>(all.data(), READ_BUFFER_BYTES), Span<char>(all.data() + READ_BUFFER_BYTES, LINE_BYTES)};
}

bool isConflictMarker(std::string_view line)
{
    for (const std::string_view marker : CONFLICT_MARKERS) {
        if (line.substr(0, marker.size()) == marker) { return true; }
    }
    return false;
}

}

KiottySpecStatus SpecFile::open(Span<const char> database)
{
    if (_work.capacity() < READ_BUFFER_BYTES + LINE_BYTES) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("work_buffer_bytes 가 97 KiB 보다 작음")});
    }
    const KiottySpecStatus status = _database.assign(database);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return _t.failure.raise(status, {literalText("경로가 너무 김")}); }
    return load();
}

void SpecFile::clear()
{
    _t.meta.clear();
    _t.checkpoints.clear();
    _t.items.clear();
    _t.relations.versions().clear();
    _t.decisions.versions().clear();
}

KiottySpecStatus SpecFile::load()
{
    clear();
    const WorkAreas areas = split(_work);
    KispFileReader reader(areas.buffer, areas.line);
    const KiottySpecStatus opened = reader.open(_database.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS && opened != KIOTTY_SPEC_STATUS_NOT_FOUND) {
        return _t.failure.raise(opened, {literalText(".kisp 를 열 수 없음: "), _database.view()});
    }
    const KiottySpecStatus read = readLines(reader);
    if (read != KIOTTY_SPEC_STATUS_SUCCESS) { return read; }
    _hash = reader.hash();
    return finishLoad();
}

KiottySpecStatus SpecFile::readLines(KispFileReader& reader)
{
    Result<bool, KiottySpecStatus> next = reader.next();
    for (; next && next.value; next = reader.next()) {
        const KiottySpecStatus status = readLine(reader);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return lineFailure(reader.lineNumber(), status); }
    }
    if (next) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    _t.failure.raise(next.code, {literalText("줄이 줄 버퍼 (64 KiB) 보다 김. .kisp 파일이 아니면 SQLite DB 일 수 있음 (tools/kisp_from_jsonl.py 참고)")});
    return lineFailure(reader.lineNumber() + 1, next.code);
}

KiottySpecStatus SpecFile::readLine(KispFileReader& reader)
{
    const std::string_view line = viewOf(reader.line());
    if (reader.lineNumber() == 1 && line != KISP_HEADER) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("첫 줄이 "), spanOf(KISP_HEADER), literalText(" 이 아님 (형식이나 모델 버전이 다름)")});
    }
    if (isConflictMarker(line)) { return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("병합 충돌 표시가 남아 있음")}); }
    if (line.empty() || line[0] == COMMENT) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto fields = reader.fields();
    if (!fields) { return _t.failure.raise(fields.code, {literalText("이스케이프가 잘못되었거나 열이 너무 많음")}); }
    return SpecParser(_t).parse(fields.value);
}

// 줄 번호를 이유 앞에 붙인다.
KiottySpecStatus SpecFile::lineFailure(size_t line_number, KiottySpecStatus status)
{
    FixedString<MAX_LINE_MESSAGE> reason;
    const Span<const char> message = _t.failure.message();
    reason.assign(Span<const char>(message.data(), message.length() < MAX_LINE_MESSAGE ? message.length() : MAX_LINE_MESSAGE));
    FixedString<MAX_LINE_MESSAGE> prefix;
    appendDecimal(prefix, line_number);
    return _t.failure.raise(status, {_database.view(), literalText(" 줄 "), prefix.view(), literalText(": "), reason.view()});
}

KiottySpecStatus SpecFile::finishLoad()
{
    ItemTables& items = _t.items.tables();
    items.versions.sort();
    items.tag_versions.sort();
    _t.relations.versions().sort();
    const bool duplicated = items.versions.findDuplicate() || items.tag_versions.findDuplicate() || _t.relations.versions().findDuplicate();
    if (duplicated) { return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("같은 레코드의 같은 체크포인트 버전이 두 줄 (병합을 확인할 것): "), _database.view()}); }
    const KiottySpecStatus rebuilt = _t.items.rebuild();
    if (rebuilt != KIOTTY_SPEC_STATUS_SUCCESS) { return rebuilt; }
    return _t.relations.rebuild();
}

KiottySpecStatus SpecFile::checkUnchanged()
{
    const WorkAreas areas = split(_work);
    const auto current = hashKispFile(_database.view(), areas.buffer);
    if (!current) { return _t.failure.raise(current.code, {literalText(".kisp 를 읽을 수 없음: "), _database.view()}); }
    if (current.value == _hash) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText(".kisp 가 연 뒤에 밖에서 바뀜 (git pull, 다른 ks). 다시 열고 쓸 것: "), _database.view()});
}

KiottySpecStatus SpecFile::save()
{
    const KiottySpecStatus unchanged = checkUnchanged();
    if (unchanged != KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
    const WorkAreas areas = split(_work);
    KispFileWriter file;
    KiottySpecStatus status = file.open(_database.view());
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return _t.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 를 쓸 수 없음: "), _database.view()}); }
    status = SpecWriter(_t, file, areas.line).write();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = file.commit();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return _t.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 를 바꿀 수 없음: "), _database.view()}); }
    _hash = file.hash();
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
