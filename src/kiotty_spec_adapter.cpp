#include "kiotty_spec_adapter.h"

#include "implementation/core/kiotty_checked.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::adapter {

namespace {

using CountResult = impl::Result<size_t, KiottySpecStatus>;

constexpr size_t MIN_COUNT = 1;

enum class Echo : uint8_t { HIDDEN, SHOWN };

size_t pickLimit(Field field);
KiottySpecSubject pickSubject(Field field);
Echo pickEcho(Field field);
bool hasNul(impl::Span<const char> text);
impl::Span<const char> echoOf(KiottySpecText text, Echo echo);
ConfigurationResult parsePools(const KiottySpecConfiguration& input);
ConfigurationResult parseBuffers(const KiottySpecConfiguration& input,
                                 const impl::Configuration& pools);
ConfigurationResult deriveCapacities(const impl::Configuration& counts);
CountResult parseCount(size_t value, size_t fallback, size_t minimum, size_t maximum);

}

HandleResult Boundary::resolve(KiottySpecText id) const
{
    const TextResult parsed = parse(id, Field::ID);
    if (!parsed) { return HandleResult::failure(parsed.code); }
    return _context.workspace().resolve(parsed.value);
}

TextResult Boundary::parse(KiottySpecText text, Field field) const
{
    const TextResult parsed = parseText(text, pickLimit(field));
    if (!parsed) {
        const impl::Span<const char> shown = echoOf(text, pickEcho(field));
        return TextResult::failure(
            _context.failure().raise(parsed.code, pickSubject(field), shown));
    }
    return parsed;
}

RecordResult Boundary::read(KiottySpecItemHandle item) const
{
    const RecordResult record = _context.workspace().items.reader().get(fromHandle(item));
    if (!record) {
        return RecordResult::failure(
            _context.failure().raise(record.code, KIOTTY_SPEC_SUBJECT_ITEM));
    }
    return record;
}

KiottySpecCursorResult Boundary::wrap(const impl::CursorIdResult& opened) const
{
    if (!opened) { return cursorFailure(opened.code); }
    const auto total = _context.cursors().total(opened.value);
    KiottySpecCursorResult result {};
    result.value = opened.value;
    result.total = total ? total.value : 0;
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    return result;
}

ConfigurationResult parseConfiguration(const KiottySpecConfiguration& input)
{
    const ConfigurationResult pools = parsePools(input);
    if (!pools) { return pools; }
    const ConfigurationResult counts = parseBuffers(input, pools.value);
    if (!counts) { return counts; }
    return deriveCapacities(counts.value);
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

impl::Span<char> toSpan(KiottySpecBuffer buffer)
{
    const bool has_data = buffer.data != nullptr;
    if (!has_data) { return impl::Span<char>(); }
    return impl::Span<char>(buffer.data, buffer.capacity);
}

impl::OptionalText toOptionalText(impl::Span<const char> text)
{
    const bool has_data = text.data() != nullptr;
    if (!has_data) { return std::nullopt; }
    return text;
}

void copyTerminated(impl::Span<char> target, impl::Span<const char> source)
{
    const size_t source_length = source.length();
    const size_t target_length = target.length();
    const size_t length = source_length < target_length ? source_length : target_length - 1;
    const auto terminator = target.at(length);
    if (!terminator) { return; }
    for (size_t index = 0; index < length; ++index) { target[index] = source[index]; }
    *terminator.value = '\0';
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

size_t pickLimit(Field field)
{
    switch (field) {
    case Field::ID: return KIOTTY_SPEC_MAX_ID_LENGTH;
    case Field::TITLE: return KIOTTY_SPEC_MAX_TITLE_LENGTH;
    case Field::SUMMARY: return KIOTTY_SPEC_MAX_SUMMARY_LENGTH;
    case Field::BODY: return KIOTTY_SPEC_MAX_BODY_LENGTH;
    case Field::DECISION: return KIOTTY_SPEC_MAX_DECISION_LENGTH;
    case Field::DESCRIPTION: return KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH;
    case Field::PATH: return impl::MAX_SYSTEM_PATH_LENGTH;
    case Field::PROJECT_NAME: return KIOTTY_SPEC_MAX_PROJECT_NAME_LENGTH;
    }
    return 0;
}

KiottySpecSubject pickSubject(Field field)
{
    switch (field) {
    case Field::ID: return KIOTTY_SPEC_SUBJECT_ID;
    case Field::TITLE: return KIOTTY_SPEC_SUBJECT_TITLE;
    case Field::SUMMARY: return KIOTTY_SPEC_SUBJECT_SUMMARY;
    case Field::BODY: return KIOTTY_SPEC_SUBJECT_BODY;
    case Field::DECISION: return KIOTTY_SPEC_SUBJECT_DECISION;
    case Field::DESCRIPTION: return KIOTTY_SPEC_SUBJECT_DESCRIPTION;
    case Field::PATH: return KIOTTY_SPEC_SUBJECT_FILE;
    case Field::PROJECT_NAME: return KIOTTY_SPEC_SUBJECT_PROJECT_NAME;
    }
    return KIOTTY_SPEC_SUBJECT_NONE;
}

Echo pickEcho(Field field)
{
    switch (field) {
    case Field::ID: return Echo::SHOWN;
    case Field::TITLE: return Echo::HIDDEN;
    case Field::SUMMARY: return Echo::HIDDEN;
    case Field::BODY: return Echo::HIDDEN;
    case Field::DECISION: return Echo::HIDDEN;
    case Field::DESCRIPTION: return Echo::HIDDEN;
    case Field::PATH: return Echo::SHOWN;
    case Field::PROJECT_NAME: return Echo::SHOWN;
    }
    return Echo::HIDDEN;
}

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

ConfigurationResult parsePools(const KiottySpecConfiguration& input)
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
    const bool has_counts = items && relations && tags && checkpoints && cursors && rows;
    if (!has_counts) { return ConfigurationResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::Configuration configuration {};
    configuration.max_items = items.value;
    configuration.max_relations = relations.value;
    configuration.max_tags = tags.value;
    configuration.max_checkpoints = checkpoints.value;
    configuration.max_cursors = cursors.value;
    configuration.max_cursor_rows = rows.value;
    return ConfigurationResult::success(configuration);
}

ConfigurationResult parseBuffers(const KiottySpecConfiguration& input,
                                 const impl::Configuration& pools)
{
    const CountResult work =
        parseCount(input.work_buffer_bytes, impl::DEFAULT_WORK_BUFFER_BYTES,
                   KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES, KIOTTY_SPEC_MAX_BUFFER_BYTES);
    const CountResult body = parseCount(input.max_body_bytes, impl::DEFAULT_MAX_BODY_BYTES,
                                        MIN_COUNT, KIOTTY_SPEC_MAX_BUFFER_BYTES);
    const bool has_counts = work && body;
    if (!has_counts) { return ConfigurationResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::Configuration configuration = pools;
    configuration.work_buffer_bytes = work.value;
    configuration.max_body_bytes = body.value;
    return ConfigurationResult::success(configuration);
}

ConfigurationResult deriveCapacities(const impl::Configuration& counts)
{
    const CountResult item_versions = impl::checkedMultiply(counts.max_items, impl::VERSION_FACTOR);
    const CountResult tag_versions = impl::checkedMultiply(counts.max_tags, impl::VERSION_FACTOR);
    const CountResult relation_versions =
        impl::checkedMultiply(counts.max_relations, impl::VERSION_FACTOR);
    const CountResult pool_rows =
        impl::checkedMultiply(counts.max_cursors, counts.max_cursor_rows);
    const bool has_derived_counts =
        item_versions && tag_versions && relation_versions && pool_rows;
    if (!has_derived_counts) {
        return ConfigurationResult::failure(KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW);
    }
    impl::Configuration configuration = counts;
    configuration.max_item_versions = item_versions.value;
    configuration.max_tag_versions = tag_versions.value;
    configuration.max_relation_versions = relation_versions.value;
    configuration.cursor_pool_rows = pool_rows.value;
    return ConfigurationResult::success(configuration);
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
