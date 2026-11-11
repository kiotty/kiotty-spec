#include "kiotty_spec_adapter.h"

#include "implementation/core/kiotty_checked.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::adapter {

namespace {

using CountResult = impl::Result<size_t, KiottySpecStatus>;

constexpr size_t MIN_COUNT = 1;

enum class Echo : uint8_t { HIDDEN, SHOWN };

bool hasNul(impl::Span<const char> text);
impl::Span<const char> echoOf(KiottySpecText text, Echo echo);
TextResult parseField(impl::Context& context, KiottySpecText text, size_t max_length,
                      KiottySpecSubject subject, Echo echo);
CountResult parseCount(size_t value, size_t fallback, size_t minimum, size_t maximum);

}

TextResult parseTitle(impl::Context& context, KiottySpecText title)
{
    return parseField(context, title, KIOTTY_SPEC_MAX_TITLE_LENGTH, KIOTTY_SPEC_SUBJECT_TITLE,
                      Echo::HIDDEN);
}

TextResult parseSummary(impl::Context& context, KiottySpecText summary)
{
    return parseField(context, summary, KIOTTY_SPEC_MAX_SUMMARY_LENGTH, KIOTTY_SPEC_SUBJECT_SUMMARY,
                      Echo::HIDDEN);
}

TextResult parseBody(impl::Context& context, KiottySpecText body)
{
    return parseField(context, body, KIOTTY_SPEC_MAX_BODY_LENGTH, KIOTTY_SPEC_SUBJECT_BODY,
                      Echo::HIDDEN);
}

TextResult parseDecision(impl::Context& context, KiottySpecText decision)
{
    return parseField(context, decision, KIOTTY_SPEC_MAX_DECISION_LENGTH,
                      KIOTTY_SPEC_SUBJECT_DECISION, Echo::HIDDEN);
}

TextResult parseDescription(impl::Context& context, KiottySpecText description)
{
    return parseField(context, description, KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH,
                      KIOTTY_SPEC_SUBJECT_DESCRIPTION, Echo::HIDDEN);
}

TextResult parsePath(impl::Context& context, KiottySpecText path)
{
    return parseField(context, path, impl::MAX_SYSTEM_PATH_LENGTH, KIOTTY_SPEC_SUBJECT_FILE,
                      Echo::SHOWN);
}

TextResult parseProjectName(impl::Context& context, KiottySpecText project_name)
{
    return parseField(context, project_name, KIOTTY_SPEC_MAX_PROJECT_NAME_LENGTH,
                      KIOTTY_SPEC_SUBJECT_PROJECT_NAME, Echo::SHOWN);
}

ConfigurationResult parseConfiguration(const KiottySpecConfiguration& input)
{
    const CountResult items = parseCount(input.max_items, impl::DEFAULT_MAX_ITEMS, MIN_COUNT,
                                         KIOTTY_SPEC_MAX_POOL_CAPACITY);
    const CountResult relations = parseCount(input.max_relations, impl::DEFAULT_MAX_RELATIONS,
                                             MIN_COUNT, KIOTTY_SPEC_MAX_POOL_CAPACITY);
    const CountResult tags = parseCount(input.max_tags, impl::DEFAULT_MAX_TAGS, MIN_COUNT,
                                        KIOTTY_SPEC_MAX_POOL_CAPACITY);
    const CountResult checkpoints = parseCount(input.max_checkpoints, impl::DEFAULT_MAX_CHECKPOINTS,
                                               MIN_COUNT, KIOTTY_SPEC_MAX_POOL_CAPACITY);
    const CountResult cursors = parseCount(input.max_cursors, impl::DEFAULT_MAX_CURSORS, MIN_COUNT,
                                           KIOTTY_SPEC_MAX_CURSOR_CAPACITY);
    const CountResult rows = parseCount(input.max_cursor_rows, impl::DEFAULT_MAX_CURSOR_ROWS,
                                        MIN_COUNT, KIOTTY_SPEC_MAX_POOL_CAPACITY);
    const CountResult work =
        parseCount(input.work_buffer_bytes, impl::DEFAULT_WORK_BUFFER_BYTES,
                   KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES, KIOTTY_SPEC_MAX_BUFFER_BYTES);
    const CountResult body = parseCount(input.max_body_bytes, impl::DEFAULT_MAX_BODY_BYTES,
                                        MIN_COUNT, KIOTTY_SPEC_MAX_BUFFER_BYTES);
    const bool counted =
        items && relations && tags && checkpoints && cursors && rows && work && body;
    if (!counted) { return ConfigurationResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const CountResult item_versions = impl::checkedMultiply(items.value, impl::VERSION_FACTOR);
    const CountResult tag_versions = impl::checkedMultiply(tags.value, impl::VERSION_FACTOR);
    const CountResult relation_versions =
        impl::checkedMultiply(relations.value, impl::VERSION_FACTOR);
    const CountResult pool_rows = impl::checkedMultiply(cursors.value, rows.value);
    const bool derived = item_versions && tag_versions && relation_versions && pool_rows;
    if (!derived) { return ConfigurationResult::failure(KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW); }
    impl::Configuration configuration {};
    configuration.max_items = items.value;
    configuration.max_relations = relations.value;
    configuration.max_tags = tags.value;
    configuration.max_checkpoints = checkpoints.value;
    configuration.max_cursors = cursors.value;
    configuration.max_cursor_rows = rows.value;
    configuration.work_buffer_bytes = work.value;
    configuration.max_body_bytes = body.value;
    configuration.max_item_versions = item_versions.value;
    configuration.max_tag_versions = tag_versions.value;
    configuration.max_relation_versions = relation_versions.value;
    configuration.cursor_pool_rows = pool_rows.value;
    return ConfigurationResult::success(configuration);
}

entity::ItemHandle fromHandle(KiottySpecItemHandle handle) { return impl::unpackHandle(handle); }

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

KiottySpecItemHandle toHandle(entity::ItemHandle handle)
{
    const bool is_valid = handle.isValid();
    if (!is_valid) { return KIOTTY_SPEC_INVALID_ITEM_HANDLE; }
    return impl::packHandle(handle);
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

KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle) { return handle.sequence; }

KiottySpecTag toTag(KiottySpecItemHandle item, KiottySpecItemKind model, uint32_t tag,
                    uint64_t value)
{
    KiottySpecTag record {};
    record.item = item;
    record.value = value;
    record.model = model;
    record.tag = tag;
    return record;
}

KiottySpecStatusResult status(KiottySpecStatus code) { return KiottySpecStatusResult{code}; }

KiottySpecSizeResult sizeResult(KiottySpecStatus code, size_t value)
{
    KiottySpecSizeResult result {};
    result.value = value;
    result.code = code;
    return result;
}

KiottySpecCursorResult cursorResult(impl::Context& context, const impl::CursorIdResult& opened)
{
    if (!opened) { return cursorFailure(opened.code); }
    const auto total = context.cursors().total(opened.value);
    KiottySpecCursorResult result {};
    result.value = opened.value;
    result.total = total ? total.value : 0;
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    return result;
}

KiottySpecCursorResult cursorFailure(KiottySpecStatus code)
{
    KiottySpecCursorResult result {};
    result.code = code;
    return result;
}

KiottySpecNameResult nameResult(std::string_view name)
{
    KiottySpecNameResult result {};
    result.code = name.empty() ? KIOTTY_SPEC_STATUS_INVALID_ARGUMENT : KIOTTY_SPEC_STATUS_SUCCESS;
    copyTerminated(impl::Span<char>(result.value.value, sizeof(result.value.value)),
                   impl::spanOf(name));
    return result;
}

bool isKind(uint32_t kind) { return entity::isValidKind(kind); }

impl::Result<entity::ItemHandle, KiottySpecStatus> resolve(impl::Context& context,
                                                           KiottySpecText id)
{
    const TextResult parsed = parseId(context, id);
    if (!parsed) {
        return impl::Result<entity::ItemHandle, KiottySpecStatus>::failure(parsed.code);
    }
    return context.workspace().resolve(parsed.value);
}

TextResult parseId(impl::Context& context, KiottySpecText id)
{
    return parseField(context, id, KIOTTY_SPEC_MAX_ID_LENGTH, KIOTTY_SPEC_SUBJECT_ID, Echo::SHOWN);
}

void copyTerminated(impl::Span<char> target, impl::Span<const char> source)
{
    const size_t target_length = target.length();
    if (target_length == 0) { return; }
    const size_t length = source.length() < target.length() ? source.length() : target.length() - 1;
    for (size_t index = 0; index < length; ++index) { target[index] = source[index]; }
    target[length] = '\0';
}

namespace {

TextResult parseField(impl::Context& context, KiottySpecText text, size_t max_length,
                      KiottySpecSubject subject, Echo echo)
{
    const TextResult parsed = parseText(text, max_length);
    if (!parsed) {
        return TextResult::failure(
            context.failure().raise(parsed.code, subject, echoOf(text, echo)));
    }
    return parsed;
}

}

TextResult parseText(KiottySpecText text, size_t max_length)
{
    if (text.data == nullptr && text.length > 0) {
        return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    if (text.length > max_length) {
        return TextResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
    }
    const impl::Span<const char> span(text.data, text.length);
    const bool has_nul = hasNul(span);
    if (has_nul) { return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const bool is_valid_utf8 = impl::isValidUtf8(span);
    if (!is_valid_utf8) { return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_UTF8); }
    return TextResult::success(span);
}

namespace {

impl::Span<const char> echoOf(KiottySpecText text, Echo echo)
{
    if (echo == Echo::HIDDEN || text.data == nullptr) { return impl::Span<const char>(); }
    const impl::Span<const char> span(text.data, text.length);
    const impl::Span<const char> shown =
        entity::cutAtCharacter(span, entity::MAX_FAILURE_TEXT_LENGTH);
    const bool has_nul = hasNul(shown);
    if (has_nul) { return impl::Span<const char>(); }
    const bool is_valid_utf8 = impl::isValidUtf8(shown);
    if (!is_valid_utf8) { return impl::Span<const char>(); }
    return shown;
}

bool hasNul(impl::Span<const char> text)
{
    for (const char letter : text) {
        if (letter == '\0') { return true; }
    }
    return false;
}

CountResult parseCount(size_t value, size_t fallback, size_t minimum, size_t maximum)
{
    if (value == 0) { return CountResult::success(fallback); }
    if (value < minimum || value > maximum) {
        return CountResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return CountResult::success(value);
}

}

}
