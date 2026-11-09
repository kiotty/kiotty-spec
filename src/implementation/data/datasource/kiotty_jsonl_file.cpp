#include "implementation/data/datasource/kiotty_jsonl_file.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char JSONL_EXTENSION[] = ".jsonl";
constexpr size_t MAX_TABLE_NAME = 64;

KiottySpecStatus validate(JsonReader& reader, Span<char> scratch)
{
    JsonField field;
    Result<bool, KiottySpecStatus> next = reader.next(field, scratch);
    while (next && next.value) { next = reader.next(field, scratch); }
    reader.rewind();
    return next ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

}

SystemPathResult jsonlPath(Span<const char> directory, std::string_view table)
{
    FixedString<MAX_TABLE_NAME> name;
    const KiottySpecStatus named = name.assign(spanOf(table));
    const KiottySpecStatus extended = name.append(literalText(JSONL_EXTENSION));
    if (named != KIOTTY_SPEC_STATUS_SUCCESS || extended != KIOTTY_SPEC_STATUS_SUCCESS) {
        return SystemPathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
    }
    return joinPath(directory, name.view());
}

KiottySpecStatus JsonlFile::open(Span<const char> directory, std::string_view table)
{
    const SystemPathResult path = jsonlPath(directory, table);
    if (!path) { return path.code; }
    _present = isFile(path.value.view());
    if (!_present) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _file.open(path.value.view()) == KIOTTY_SPEC_STATUS_SUCCESS ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

Result<bool, KiottySpecStatus> JsonlFile::next()
{
    using RowResult = Result<bool, KiottySpecStatus>;
    if (!_present) { return RowResult::success(false); }
    while (true) {
        const RowResult read = _file.next();
        if (!read || !read.value) { return read; }
        if (_file.line().length() == 0) { continue; }
        _row = JsonReader(_file.line());
        const KiottySpecStatus checked = validate(_row, _scratch);
        if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return RowResult::failure(checked); }
        return RowResult::success(true);
    }
}

}
