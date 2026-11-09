// C ↔ implementation 얇은 어댑터. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다. 예외 없음.

#include "kiotty_spec.h"

#include "implementation/context/kiotty_context.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstring>
#include <memory>
#include <string>

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;

struct KiottySpecContext {
    explicit KiottySpecContext(const impl::Configuration& configuration) : context(configuration) {}
    impl::Context context;
};

namespace {

constexpr uint32_t SHIFT = 32;

impl::Span<const char> spanOf(KiottySpecText text)
{
    if (text.data == nullptr) { return impl::Span<const char>(); }
    return impl::Span<const char>(text.data, text.length);
}

template <size_t Capacity, size_t Size>
void copyText(char (&target)[Size], const impl::FixedString<Capacity>& source)
{
    static_assert(Size == Capacity + 1, "buffer must match FixedString capacity");
    std::memcpy(target, source.c_str(), source.length());
    target[source.length()] = '\0';
}

KiottySpecItemHandle toHandle(entity::ItemHandle handle)
{
    if (!handle.isValid()) { return KIOTTY_SPEC_INVALID_ITEM_HANDLE; }
    return impl::packHandle(handle);
}

entity::ItemHandle fromHandle(KiottySpecItemHandle handle) { return impl::unpackHandle(handle); }

KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle) { return handle.sequence; }

KiottySpecItem toItem(const entity::ItemRecord& record)
{
    KiottySpecItem item {};
    item.checkpoint = toCheckpoint(record.checkpoint);
    item.parent = toHandle(record.parent);
    item.kind = static_cast<KiottySpecItemKind>(record.kind);
    copyText(item.id, record.id);
    copyText(item.title, record.title);
    copyText(item.summary, record.summary);
    return item;
}

KiottySpecCheckpoint toCheckpointRecord(const entity::CheckpointRecord& record)
{
    KiottySpecCheckpoint checkpoint {};
    checkpoint.created_at = record.created_at;
    checkpoint.previous = toCheckpoint(record.previous);
    checkpoint.step = static_cast<KiottySpecStep>(record.step);
    checkpoint.state = static_cast<KiottySpecCheckpointState>(record.state);
    copyText(checkpoint.description, record.description);
    return checkpoint;
}

KiottySpecStatusResult status(KiottySpecStatus code) { return KiottySpecStatusResult{code}; }

KiottySpecCursorResult cursorResult(impl::Context& context, const impl::CursorIdResult& opened)
{
    if (!opened) { return KiottySpecCursorResult{opened.code, 0, 0}; }
    const auto total = context.cursors().total(opened.value);
    return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_SUCCESS, opened.value, total ? total.value : 0};
}

bool isKind(uint32_t kind) { return entity::isValidKind(kind); }

impl::Result<entity::ItemHandle, KiottySpecStatus> resolve(impl::Context& context, KiottySpecText id)
{
    const auto found = context.workspace().items.reader().find(spanOf(id));
    if (!found) {
        return impl::Result<entity::ItemHandle, KiottySpecStatus>::failure(
            context.failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {impl::literalText("없는 항목: "), spanOf(id)}));
    }
    return found;
}

KiottySpecSizeResult copyOut(impl::Span<const char> text, char* buffer, size_t capacity)
{
    if (buffer == nullptr || capacity <= text.length()) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, text.length() + 1}; }
    std::memcpy(buffer, text.data(), text.length());
    buffer[text.length()] = '\0';
    return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_SUCCESS, text.length()};
}

template <typename Page, typename Fill>
KiottySpecStatusResult fetchPage(impl::Context& context, KiottySpecCursor cursor, impl::CursorKind kind, Page* page, Fill fill)
{
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context.cursors().fetch(cursor, kind, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        if (fill(row, page->length)) { ++page->length; }
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}

extern "C" {

KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration* configuration)
{
    if (configuration == nullptr) { return KiottySpecContextResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, nullptr}; }
    impl::Configuration settings;
    if (configuration->max_items != 0) { settings.maxItems = configuration->max_items; }
    if (configuration->max_relations != 0) { settings.maxRelations = configuration->max_relations; }
    if (configuration->max_tags != 0) { settings.maxTags = configuration->max_tags; }
    if (configuration->max_checkpoints != 0) { settings.maxCheckpoints = configuration->max_checkpoints; }
    if (configuration->max_cursors != 0) { settings.maxCursors = configuration->max_cursors; }
    if (configuration->max_cursor_rows != 0) { settings.maxCursorRows = configuration->max_cursor_rows; }
    if (configuration->work_buffer_bytes != 0) { settings.workBufferBytes = configuration->work_buffer_bytes; }
    // 할당 실패는 process 종료다 (개발수칙 1.1). 소유권은 releaseContext 가 되찾는다.
    KiottySpecContext* context = std::make_unique<KiottySpecContext>(settings).release();
    const KiottySpecStatus opened = context->context.open(spanOf(configuration->project_root));
    // 실패해도 컨텍스트를 돌려주어 readLastError 로 이유를 읽게 한다. 호출자가 releaseContext 로 닫는다.
    return KiottySpecContextResult{opened, context};
}

void KiottySpec_releaseContext(KiottySpecContext* context) { const std::unique_ptr<KiottySpecContext> owner(context); }

KiottySpecSizeResult KiottySpec_readLastError(KiottySpecContext* context, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    return copyOut(context->context.failure().message(), buffer, capacity);
}

const char* KiottySpec_statusName(KiottySpecStatus code)
{
    switch (code) {
    case KIOTTY_SPEC_STATUS_SUCCESS: return "SUCCESS";
    case KIOTTY_SPEC_STATUS_NOT_FOUND: return "NOT_FOUND";
    case KIOTTY_SPEC_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case KIOTTY_SPEC_STATUS_CONFIRMED: return "CONFIRMED";
    case KIOTTY_SPEC_STATUS_PERMISSION: return "PERMISSION";
    case KIOTTY_SPEC_STATUS_PRECONDITION: return "PRECONDITION";
    case KIOTTY_SPEC_STATUS_CONFLICT: return "CONFLICT";
    case KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE: return "WRONG_CURSOR_TYPE";
    case KIOTTY_SPEC_STATUS_INPUT_OUTPUT: return "INPUT_OUTPUT";
    case KIOTTY_SPEC_STATUS_DATABASE: return "DATABASE";
    case KIOTTY_SPEC_STATUS_OUT_OF_RANGE: return "OUT_OF_RANGE";
    case KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
    case KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW: return "ARITHMETIC_OVERFLOW";
    case KIOTTY_SPEC_STATUS_POOL_IS_FULL: return "POOL_IS_FULL";
    case KIOTTY_SPEC_STATUS_INVALID_HANDLE: return "INVALID_HANDLE";
    case KIOTTY_SPEC_STATUS_INVALID_UTF8: return "INVALID_UTF8";
    }
    return "UNKNOWN";
}

KiottySpecStatusResult KiottySpec_initProject(KiottySpecContext* context, KiottySpecText project_name, bool install_git_hooks)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.init(spanOf(project_name), install_git_hooks));
}

const char* KiottySpec_kindName(KiottySpecItemKind kind)
{
    const std::string_view name = entity::kindName(static_cast<entity::ItemKind>(kind));
    return name.empty() ? "" : name.data();
}

KiottySpecItemKind KiottySpec_parseKind(KiottySpecText name)
{
    return static_cast<KiottySpecItemKind>(entity::parseKind(impl::viewOf(spanOf(name))));
}

const char* KiottySpec_checkpointStateName(KiottySpecCheckpointState state)
{
    const std::string_view name = entity::stateName(static_cast<entity::CheckpointState>(state));
    return name.empty() ? "" : name.data();
}

KiottySpecIdResult KiottySpec_createItem(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText title, KiottySpecText summary)
{
    KiottySpecIdResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto created = context->context.editor().createItem(static_cast<entity::ItemKind>(kind), spanOf(title), spanOf(summary));
    result.code = created.code;
    if (created) { copyText(result.value, created.value); }
    return result;
}

KiottySpecItemResult KiottySpec_readItemByHandle(KiottySpecContext* context, KiottySpecItemHandle item)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto record = context->context.workspace().items.reader().get(fromHandle(item));
    result.code = record.code;
    if (record) {
        result.handle = item;
        result.value = toItem(*record.value);
    }
    return result;
}

KiottySpecItemResult KiottySpec_readItem(KiottySpecContext* context, KiottySpecText id)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto handle = resolve(context->context, id);
    if (!handle) {
        result.code = handle.code;
        return result;
    }
    return KiottySpec_readItemByHandle(context, toHandle(handle.value));
}

KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return KiottySpecItemHandleResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_INVALID_ITEM_HANDLE}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecItemHandleResult{handle.code, KIOTTY_SPEC_INVALID_ITEM_HANDLE}; }
    return KiottySpecItemHandleResult{KIOTTY_SPEC_STATUS_SUCCESS, toHandle(handle.value)};
}

KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext* context, KiottySpecText id, KiottySpecText title, KiottySpecText summary)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().updateItem(spanOf(id), spanOf(title), spanOf(summary)));
}

KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext* context, KiottySpecText id, KiottySpecText parent_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().setParent(spanOf(id), spanOf(parent_id)));
}

KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().removeItem(spanOf(id)));
}

KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext* context, KiottySpecText id, KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().writeBody(spanOf(id), spanOf(text)));
}

KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext* context, KiottySpecText id, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecSizeResult{handle.code, 0}; }
    const auto read = context->context.workspace().items.reader().readBody(handle.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecStatusResult KiottySpec_setItemTag(KiottySpecContext* context, KiottySpecItemHandle item, uint32_t tag, uint64_t value)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().setTag(fromHandle(item), tag, value));
}

KiottySpecTagResult KiottySpec_readItemTag(KiottySpecContext* context, KiottySpecItemHandle item, uint32_t tag)
{
    KiottySpecTagResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Workspace& workspace = context->context.workspace();
    const auto record = workspace.items.reader().get(fromHandle(item));
    if (!record) {
        result.code = record.code;
        return result;
    }
    if (tag >= workspace.catalog.model(record.value->kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("모델에 없는 태그 index")});
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.value = KiottySpecTag{item, static_cast<KiottySpecItemKind>(record.value->kind), tag, workspace.items.tags().value(fromHandle(item), tag)};
    return result;
}

KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext* context, KiottySpecItemHandle item)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    if (!context->context.workspace().items.reader().get(fromHandle(item))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_HANDLE, 0, 0}; }
    return cursorResult(context->context, context->context.finder().tags(fromHandle(item)));
}

KiottySpecStatusResult KiottySpec_fetchTagPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecTagPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return fetchPage(context->context, cursor, impl::CursorKind::TAG, page, [&](const impl::CursorRow& row, size_t index) {
        page->elements[index] = KiottySpecTag{row.first, static_cast<KiottySpecItemKind>(row.second & 0xFFFFFFFFu),
                                              static_cast<uint32_t>(row.second >> SHIFT), row.third};
        return true;
    });
}

KiottySpecStatusResult KiottySpec_addRelation(KiottySpecContext* context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.relationEditor().add(spanOf(from_id), relation, spanOf(to_id)));
}

KiottySpecStatusResult KiottySpec_removeRelation(KiottySpecContext* context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.relationEditor().remove(spanOf(from_id), relation, spanOf(to_id)));
}

KiottySpecCursorResult KiottySpec_openRelationCursor(KiottySpecContext* context, KiottySpecText id, KiottySpecDirection direction)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    if (direction < KIOTTY_SPEC_DIRECTION_UPSTREAM || direction > KIOTTY_SPEC_DIRECTION_BOTH) {
        return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0};
    }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecCursorResult{handle.code, 0, 0}; }
    return cursorResult(context->context, context->context.finder().relations(handle.value, direction));
}

KiottySpecCursorResult KiottySpec_openRelationCursorByModel(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t relation)
{
    if (context == nullptr || !isKind(kind)) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    if (relation >= context->context.catalog().model(static_cast<entity::ItemKind>(kind)).relations.length()) {
        return KiottySpecCursorResult{
            context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("모델에 없는 관계 index")}), 0, 0};
    }
    return cursorResult(context->context, context->context.finder().relationsByModel(static_cast<entity::ItemKind>(kind), relation));
}

KiottySpecStatusResult KiottySpec_fetchRelationPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecRelationPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return fetchPage(context->context, cursor, impl::CursorKind::RELATION, page, [&](const impl::CursorRow& row, size_t index) {
        page->elements[index] = KiottySpecRelation{row.first, row.second, static_cast<KiottySpecItemKind>(row.third & 0xFFFFFFFFu),
                                                   static_cast<uint32_t>(row.third >> SHIFT)};
        return true;
    });
}

KiottySpecCursorResult KiottySpec_openItemCursor(KiottySpecContext* context, KiottySpecItemKind kind, const KiottySpecTagQuery* tag_queries, size_t count)
{
    if (context == nullptr || (kind != 0 && !isKind(kind)) || count > KIOTTY_SPEC_MAX_TAG_QUERIES || (count > 0 && tag_queries == nullptr)) {
        return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0};
    }
    impl::TagQuery queries[KIOTTY_SPEC_MAX_TAG_QUERIES] {};
    for (size_t index = 0; index < count; ++index) {
        if (!isKind(tag_queries[index].kind)) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
        queries[index] = impl::TagQuery{static_cast<entity::ItemKind>(tag_queries[index].kind), tag_queries[index].tag_index, tag_queries[index].mask};
    }
    return cursorResult(context->context, context->context.finder().items(kind, impl::Span<const impl::TagQuery>(queries, count)));
}

KiottySpecCursorResult KiottySpec_openChildCursor(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecCursorResult{handle.code, 0, 0}; }
    return cursorResult(context->context, context->context.finder().children(handle.value));
}

KiottySpecCursorResult KiottySpec_openImpactCursor(KiottySpecContext* context, KiottySpecText id, uint32_t depth)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecCursorResult{handle.code, 0, 0}; }
    return cursorResult(context->context, context->context.finder().impact(handle.value, depth));
}

KiottySpecStatusResult KiottySpec_fetchItemPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecItemPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    return fetchPage(context->context, cursor, impl::CursorKind::ITEM, page, [&](const impl::CursorRow& row, size_t index) {
        const auto record = items.reader().get(fromHandle(row.first));
        if (!record) { return false; }
        page->elements[index] = toItem(*record.value);
        return true;
    });
}

KiottySpecStatusResult KiottySpec_fetchIdPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecIdPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    return fetchPage(context->context, cursor, impl::CursorKind::ID, page, [&](const impl::CursorRow& row, size_t index) {
        const auto record = items.reader().get(fromHandle(row.first));
        if (!record) { return false; }
        copyText(page->elements[index], record.value->id);
        return true;
    });
}

KiottySpecStatusResult KiottySpec_closeCursor(KiottySpecContext* context, KiottySpecCursor cursor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.cursors().close(cursor));
}

KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext* context, KiottySpecStep step, KiottySpecText description)
{
    if (context == nullptr) { return KiottySpecCheckpointHandleResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto started = context->context.lifecycle().start(static_cast<entity::Step>(step), spanOf(description));
    return KiottySpecCheckpointHandleResult{started.code, started ? started.value : 0};
}

KiottySpecStatusResult KiottySpec_resumeCheckpoint(KiottySpecContext* context)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().resume());
}

KiottySpecStatusResult KiottySpec_completeCheckpoint(KiottySpecContext* context, KiottySpecActor actor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().complete(actor));
}

KiottySpecCheckpointResult KiottySpec_readCheckpoint(KiottySpecContext* context, KiottySpecCheckpointHandle handle)
{
    KiottySpecCheckpointResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    const uint32_t sequence = handle == KIOTTY_SPEC_CHECKPOINT_CURRENT ? checkpoints.latest() : static_cast<uint32_t>(handle & 0xFFFFFFFFu);
    const auto record = checkpoints.get(sequence);
    if (!record) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {impl::literalText("없는 체크포인트")});
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.handle = sequence;
    result.value = toCheckpointRecord(*record.value);
    return result;
}

KiottySpecCursorResult KiottySpec_openCheckpointCursor(KiottySpecContext* context, KiottySpecStep step)
{
    if (context == nullptr || (step != 0 && !entity::isValidStep(step))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    return cursorResult(context->context, context->context.finder().history().checkpoints(step));
}

KiottySpecStatusResult KiottySpec_fetchCheckpointPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecCheckpointPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    return fetchPage(context->context, cursor, impl::CursorKind::CHECKPOINT, page, [&](const impl::CursorRow& row, size_t index) {
        const entity::CheckpointRecord* record = checkpoints.at(static_cast<uint32_t>(row.number));
        if (record == nullptr) { return false; }
        page->handles[index] = static_cast<KiottySpecCheckpointHandle>(row.number);
        page->elements[index] = toCheckpointRecord(*record);
        return true;
    });
}

KiottySpecSizeResult KiottySpec_pruneCheckpoints(KiottySpecContext* context, KiottySpecActor actor, KiottySpecCheckpointHandle base)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto pruned = context->context.lifecycle().prune(actor, static_cast<uint32_t>(base & 0xFFFFFFFFu));
    return KiottySpecSizeResult{pruned.code, pruned ? pruned.value : 0};
}

KiottySpecStatusResult KiottySpec_writeDecision(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().writeDecision(static_cast<entity::ItemKind>(kind), spanOf(text)));
}

KiottySpecSizeResult KiottySpec_readDecision(KiottySpecContext* context, KiottySpecItemKind kind, char* buffer, size_t capacity)
{
    if (context == nullptr || !isKind(kind) || !entity::hasDecision(static_cast<entity::ItemKind>(kind))) {
        return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0};
    }
    const auto read = context->context.workspace().decisions.read(static_cast<entity::ItemKind>(kind), buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecModelResult KiottySpec_readModel(KiottySpecContext* context, KiottySpecItemKind kind)
{
    KiottySpecModelResult result {};
    if (context == nullptr || !isKind(kind)) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, model.name);
    copyText(result.value.id_prefix, model.id_prefix);
    result.value.kind = kind;
    result.value.created_in = static_cast<KiottySpecStep>(model.created_in);
    result.value.tag_count = static_cast<uint32_t>(model.tags.length());
    result.value.relation_count = static_cast<uint32_t>(model.relations.length());
    return result;
}

KiottySpecStatusResult KiottySpec_readTagDefinition(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t index, KiottySpecTagDefinition* out)
{
    if (context == nullptr || out == nullptr || !isKind(kind)) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    if (index >= model.tags.length()) { return status(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
    const entity::TagDefinition& definition = model.tags[index];
    copyText(out->name, definition.name);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) { copyText(out->bit_names[bit], definition.bit_names[bit]); }
    out->cardinality = static_cast<KiottySpecCardinality>(definition.cardinality);
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecRelationDefinitionResult KiottySpec_readRelationDefinition(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t index)
{
    KiottySpecRelationDefinitionResult result {};
    if (context == nullptr || !isKind(kind)) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    if (index >= model.relations.length()) {
        result.code = KIOTTY_SPEC_STATUS_OUT_OF_RANGE;
        return result;
    }
    const entity::RelationDefinition& definition = model.relations[index];
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, definition.name);
    result.value.target_kind = static_cast<KiottySpecItemKind>(definition.target_kind);
    result.value.cardinality = static_cast<KiottySpecCardinality>(definition.cardinality);
    return result;
}

KiottySpecPositionResult KiottySpec_readPosition(KiottySpecContext* context)
{
    KiottySpecPositionResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto position = context->context.finder().history().position();
    result.code = position.code;
    if (!position) { return result; }
    result.value.current = position.value.current;
    result.value.step = static_cast<KiottySpecStep>(position.value.step);
    result.value.state = static_cast<KiottySpecCheckpointState>(position.value.state);
    result.value.next_step = static_cast<KiottySpecStep>(position.value.next_step);
    result.value.pending_writes = position.value.pending_writes;
    result.value.open_change_requests = position.value.open_change_requests;
    return result;
}

KiottySpecCursorResult KiottySpec_openStepRunCursor(KiottySpecContext* context)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    return cursorResult(context->context, context->context.finder().history().stepRuns());
}

KiottySpecStatusResult KiottySpec_fetchStepRunPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecStepRunPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return fetchPage(context->context, cursor, impl::CursorKind::STEP_RUN, page, [&](const impl::CursorRow& row, size_t index) {
        page->elements[index] = KiottySpecStepRun{row.first, row.second, static_cast<KiottySpecStep>(row.third & 0xFFFFFFFFu),
                                                  static_cast<KiottySpecCheckpointState>(row.third >> SHIFT), static_cast<uint64_t>(row.number)};
        return true;
    });
}

KiottySpecDiffSummaryResult KiottySpec_summarizeDiff(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    KiottySpecDiffSummaryResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto range = differ.range(query.from, query.to);
    if (!range) {
        result.code = range.code;
        return result;
    }
    impl::KindCounts counts[entity::ITEM_KIND_COUNT] {};
    result.code = differ.summarize(range.value, query.kind_mask, query.field_mask, counts);
    result.from = range.value.from;
    result.to = range.value.to;
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT; ++index) {
        const impl::KindCounts& count = counts[index];
        result.kinds[index] = KiottySpecKindDiff{count.added, count.modified, count.removed, count.relations_added, count.relations_removed, count.decision_changed};
    }
    return result;
}

KiottySpecCursorResult KiottySpec_openItemDiffCursor(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto range = context->context.differ().range(query.from, query.to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, context->context.differ().items(range.value, query.kind_mask, query.field_mask));
}

KiottySpecStatusResult KiottySpec_fetchItemDiffPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecItemDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    return fetchPage(context->context, cursor, impl::CursorKind::ITEM_DIFF, page, [&](const impl::CursorRow& row, size_t index) {
        const impl::ItemDiffRow diff = impl::unpackItemDiff(row);
        KiottySpecItemDiff& element = page->elements[index];
        element = KiottySpecItemDiff{};
        element.before = diff.before;
        element.after = diff.after;
        const auto handle = items.reader().find(diff.id.view());
        element.item = handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
        element.kind = static_cast<KiottySpecItemKind>(diff.kind);
        element.change = static_cast<KiottySpecChange>(diff.change);
        element.changed_fields = diff.fields;
        copyText(element.id, diff.id);
        return true;
    });
}

KiottySpecCursorResult KiottySpec_openRelationDiffCursor(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto range = context->context.differ().range(query.from, query.to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, context->context.differ().relations(range.value, query.kind_mask));
}

KiottySpecStatusResult KiottySpec_fetchRelationDiffPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecRelationDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return fetchPage(context->context, cursor, impl::CursorKind::RELATION_DIFF, page, [&](const impl::CursorRow& row, size_t index) {
        const impl::RelationDiffRow diff = impl::unpackRelationDiff(row);
        KiottySpecRelationDiff& element = page->elements[index];
        element = KiottySpecRelationDiff{};
        element.checkpoint = diff.checkpoint;
        element.model = static_cast<KiottySpecItemKind>(diff.model);
        element.relation = diff.relation;
        element.change = static_cast<KiottySpecChange>(diff.change);
        copyText(element.from_id, diff.from_id);
        copyText(element.to_id, diff.to_id);
        return true;
    });
}

KiottySpecItemResult KiottySpec_readItemAt(KiottySpecContext* context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(spanOf(id), point.value);
    result.code = version.code;
    if (!version) { return result; }
    const auto handle = context->context.workspace().items.reader().find(spanOf(id));
    result.handle = handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
    result.value = toItem(version.value.record);
    return result;
}

KiottySpecTagResult KiottySpec_readItemTagAt(KiottySpecContext* context, KiottySpecText id, uint32_t tag, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecTagResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(spanOf(id), point.value);
    if (!version) {
        result.code = version.code;
        return result;
    }
    if (tag >= context->context.catalog().model(version.value.record.kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("모델에 없는 태그 index")});
        return result;
    }
    const auto value = differ.past().tagAt(spanOf(id), tag, point.value);
    result.code = value.code;
    const auto handle = context->context.workspace().items.reader().find(spanOf(id));
    result.value = KiottySpecTag{handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE, static_cast<KiottySpecItemKind>(version.value.record.kind),
                                 tag, value ? value.value : 0};
    return result;
}

KiottySpecSizeResult KiottySpec_readItemBodyAt(KiottySpecContext* context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return KiottySpecSizeResult{point.code, 0}; }
    const auto read = differ.past().bodyAt(spanOf(id), point.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecSizeResult KiottySpec_readDecisionAt(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecCheckpointHandle checkpoint, char* buffer, size_t capacity)
{
    if (context == nullptr || !isKind(kind) || !entity::hasDecision(static_cast<entity::ItemKind>(kind))) {
        return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0};
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return KiottySpecSizeResult{point.code, 0}; }
    const auto read = differ.past().decisionAt(static_cast<entity::ItemKind>(kind), point.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecCursorResult KiottySpec_openTextDiffCursor(KiottySpecContext* context, KiottySpecItemHandle item, KiottySpecTextField field,
                                                     KiottySpecCheckpointHandle from, KiottySpecCheckpointHandle to)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    impl::Differ& differ = context->context.differ();
    if (field != KIOTTY_SPEC_TEXT_FIELD_TITLE && field != KIOTTY_SPEC_TEXT_FIELD_SUMMARY) {
        return KiottySpecCursorResult{context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("글자 diff 는 제목이나 요약 하나에만 쓴다")}), 0, 0};
    }
    if (!context->context.workspace().items.reader().get(fromHandle(item))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_HANDLE, 0, 0}; }
    const auto range = differ.range(from, to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, differ.text().open(fromHandle(item), static_cast<uint32_t>(field), range.value));
}

KiottySpecTextDiffResult KiottySpec_nextTextDiff(KiottySpecContext* context, KiottySpecCursor cursor)
{
    KiottySpecTextDiffResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto piece = context->context.differ().text().next(cursor);
    result.code = piece.code;
    if (piece) { result.value = KiottySpecTextDiff{piece.value.text, piece.value.length, static_cast<KiottySpecTextChange>(piece.value.change), 0}; }
    return result;
}

KiottySpecStatusResult KiottySpec_closeTextDiffCursor(KiottySpecContext* context, KiottySpecCursor cursor) { return KiottySpec_closeCursor(context, cursor); }

KiottySpecStatusResult KiottySpec_exportJsonl(KiottySpecContext* context, KiottySpecText directory)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (directory.length > 0) { return status(context->context.exchange().exportJsonl(spanOf(directory))); }
    const auto fallback = context->context.jsonlDirectory();
    if (!fallback) { return status(fallback.code); }
    return status(context->context.exchange().exportJsonl(fallback.value.view()));
}

KiottySpecSizeResult KiottySpec_importJsonl(KiottySpecContext* context, KiottySpecText directory, char* report, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    // 보고는 호출자 버퍼에 바로 쓴다. 버퍼가 없으면 버린다.
    char discard[1] {};
    const impl::Span<char> area = report != nullptr && capacity > 0 ? impl::Span<char>(report, capacity) : impl::Span<char>(discard, 1);
    impl::Result<size_t, KiottySpecStatus> imported = impl::Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    if (directory.length > 0) {
        imported = context->context.exchange().importJsonl(spanOf(directory), area);
    } else {
        const auto fallback = context->context.jsonlDirectory();
        if (!fallback) { return KiottySpecSizeResult{fallback.code, 0}; }
        imported = context->context.exchange().importJsonl(fallback.value.view(), area);
    }
    return KiottySpecSizeResult{imported.code, imported ? imported.value : 0};
}

}
