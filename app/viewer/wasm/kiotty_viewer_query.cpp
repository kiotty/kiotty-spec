#include "kiotty_viewer_query.h"

#include "kiotty_viewer_names.h"

#include <memory>

namespace kiotty::viewer {

namespace {

constexpr uint32_t FIRST_KIND = 1;
constexpr KiottySpecStep EVERY_STEP = static_cast<KiottySpecStep>(0);

}

QueryWriter::QueryWriter(KiottySpecContext& context, const Project& project, std::string& out)
    : _context(context), _project(project), _json(out), _definitions(context)
{
}

void QueryWriter::writeOverview()
{
    _json.open(JsonScope::OBJECT);
    writeSuccess();
    writeProject();
    _json.writeKey("kinds");
    _json.open(JsonScope::ARRAY);
    for (uint32_t number = FIRST_KIND; number <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++number) {
        writeKindCount(static_cast<KiottySpecItemKind>(number));
    }
    _json.close(JsonScope::ARRAY);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeProgress()
{
    _json.open(JsonScope::OBJECT);
    writeSuccess();
    writePosition();
    writeUnmetConditions();
    writeCheckpoints();
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeSuccess()
{
    _json.writeKey("ok");
    _json.writeLiteral("true");
}

void QueryWriter::writeProject()
{
    const bool has_name = !_project.name.empty();
    const bool has_created_at = _project.has_created_at;
    const uint64_t created_at = static_cast<uint64_t>(_project.created_at);
    _json.writeKey("project");
    _json.open(JsonScope::OBJECT);
    _json.writeKey("name");
    if (has_name) { _json.writeString(_project.name); }
    else { _json.writeLiteral("null"); }
    _json.writeKey("created_at");
    if (has_created_at) { _json.writeNumber(created_at); }
    else { _json.writeLiteral("null"); }
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeKindCount(KiottySpecItemKind kind)
{
    const KiottySpecModelResult model = KiottySpec_readModel(&_context, kind);
    const bool has_model = isSuccess(model.code);
    if (!has_model) { return; }
    const KiottySpecCursorResult cursor = KiottySpec_openItemCursor(&_context, kind, nullptr, 0);
    const bool is_open = isSuccess(cursor.code);
    const uint64_t count = is_open ? cursor.total : 0;
    if (is_open) { KiottySpec_closeCursor(&_context, cursor.value); }
    _json.open(JsonScope::OBJECT);
    _json.writeKey("kind");
    _json.writeString(formatKind(kind));
    _json.writeKey("name");
    _json.writeString(model.value.name);
    _json.writeKey("prefix");
    _json.writeString(model.value.id_prefix);
    _json.writeKey("created_in");
    _json.writeNumber(static_cast<uint64_t>(model.value.created_in));
    _json.writeKey("count");
    _json.writeNumber(count);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writePosition()
{
    const KiottySpecPositionResult position = KiottySpec_readPosition(&_context);
    const bool has_checkpoint = isSuccess(position.code) && position.value.current != 0;
    _json.writeKey("position");
    if (!has_checkpoint) {
        _json.writeLiteral("null");
        return;
    }
    const KiottySpecCheckpointResult current =
        KiottySpec_readCheckpoint(&_context, position.value.current);
    const bool has_current = isSuccess(current.code);
    _json.open(JsonScope::OBJECT);
    _json.writeKey("checkpoint");
    _json.writeString(formatCheckpoint(position.value.current));
    _json.writeKey("step");
    _json.writeNumber(static_cast<uint64_t>(position.value.step));
    _json.writeKey("state");
    _json.writeString(formatState(position.value.state));
    _json.writeKey("next_step");
    _json.writeNumber(static_cast<uint64_t>(position.value.next_step));
    _json.writeKey("open_change_requests");
    _json.writeNumber(position.value.open_change_requests);
    _json.writeKey("description");
    _json.writeString(has_current ? current.value.description : "");
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeUnmetConditions()
{
    _json.writeKey("unmet");
    _json.open(JsonScope::ARRAY);
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(&_context);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) {
        _json.close(JsonScope::ARRAY);
        return;
    }
    const std::unique_ptr<KiottySpecUnmetConditionPage> page_storage =
        std::make_unique<KiottySpecUnmetConditionPage>();
    KiottySpecUnmetConditionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchUnmetConditionPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            writeUnmetRow(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeUnmetRow(const KiottySpecUnmetCondition& row)
{
    const KiottySpecItemResult item = KiottySpec_readItem(&_context, makeText(row.id));
    const bool has_item = isSuccess(item.code);
    _json.open(JsonScope::OBJECT);
    _json.writeKey("id");
    _json.writeString(row.id);
    _json.writeKey("kind");
    _json.writeString(has_item ? formatKind(item.value.kind) : "");
    _json.writeKey("title");
    _json.writeString(has_item ? item.value.title : "");
    _json.writeKey("condition");
    _json.writeString(formatCondition(row.condition));
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeCheckpoints()
{
    _json.writeKey("checkpoints");
    _json.open(JsonScope::ARRAY);
    const KiottySpecCursorResult cursor = KiottySpec_openCheckpointCursor(&_context, EVERY_STEP);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) {
        _json.close(JsonScope::ARRAY);
        return;
    }
    std::vector<KiottySpecCheckpoint> rows {};
    std::vector<KiottySpecCheckpointHandle> handles {};
    const std::unique_ptr<KiottySpecCheckpointPage> page_storage =
        std::make_unique<KiottySpecCheckpointPage>();
    KiottySpecCheckpointPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchCheckpointPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            rows.push_back(page.elements[index]);
            handles.push_back(page.handles[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    for (size_t index = rows.size(); index > 0; --index) {
        writeCheckpointRow(handles.at(index - 1), rows.at(index - 1));
    }
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeCheckpointRow(KiottySpecCheckpointHandle handle,
                                     const KiottySpecCheckpoint& row)
{
    _json.open(JsonScope::OBJECT);
    _json.writeKey("checkpoint");
    _json.writeString(formatCheckpoint(handle));
    _json.writeKey("step");
    _json.writeNumber(static_cast<uint64_t>(row.step));
    _json.writeKey("state");
    _json.writeString(formatState(row.state));
    _json.writeKey("description");
    _json.writeString(row.description);
    _json.writeKey("created_at");
    _json.writeNumber(static_cast<uint64_t>(row.created_at));
    _json.writeKey("previous");
    writeCheckpointOrNull(row.previous);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeCheckpointOrNull(KiottySpecCheckpointHandle handle)
{
    const bool has_checkpoint = handle != 0;
    if (has_checkpoint) { _json.writeString(formatCheckpoint(handle)); }
    else { _json.writeLiteral("null"); }
}

}
