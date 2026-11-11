#include "implementation/domain/usecases/kiotty_spec_parser.h"

#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/domain/entity/kiotty_item_id.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::CheckpointRecord;
using entity::ItemKind;
using entity::ItemVersion;
using entity::RelationVersion;
using entity::TagVersion;

constexpr size_t META_FIELDS = 4;
constexpr size_t CHECKPOINT_FIELDS = 8;
constexpr size_t ITEM_FIELDS = 4;
constexpr size_t ITEM_VERSION_FIELDS = 9;
constexpr size_t TAG_FIELDS = 6;
constexpr size_t RELATION_FIELDS = 7;
constexpr size_t DECISION_FIELDS = 5;
constexpr size_t BODY_FIELDS = 4;
constexpr size_t KEY_DIGITS = 8;
constexpr size_t HASH_DIGITS = 16;
constexpr uint64_t MAX_SEQUENCE = UINT32_MAX;
constexpr uint64_t MAX_EPOCH = INT64_MAX;

KiottySpecStatus invalid(entity::Failure& failure, std::string_view table)
{
    return failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE_LINE,
                         spanOf(table));
}

Result<uint32_t, KiottySpecStatus> parseKey(Span<const char> text)
{
    const auto number = parseHex(text, KEY_DIGITS);
    if (!number || number.value == entity::NO_KEY) {
        return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return Result<uint32_t, KiottySpecStatus>::success(static_cast<uint32_t>(number.value));
}

Result<uint32_t, KiottySpecStatus> parseOptionalKey(Span<const char> text)
{
    if (viewOf(text) == NONE_MARKER) {
        return Result<uint32_t, KiottySpecStatus>::success(entity::NO_KEY);
    }
    return parseKey(text);
}

Result<uint32_t, KiottySpecStatus> parseSequence(Span<const char> text)
{
    const auto number = parseDecimal(text, MAX_SEQUENCE);
    if (!number || number.value == 0) {
        return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return Result<uint32_t, KiottySpecStatus>::success(static_cast<uint32_t>(number.value));
}

Result<ItemKind, KiottySpecStatus> parseKindName(Span<const char> text)
{
    const uint32_t kind = entity::parseKind(viewOf(text));
    if (kind == 0) {
        return Result<ItemKind, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return Result<ItemKind, KiottySpecStatus>::success(static_cast<ItemKind>(kind));
}

bool fits(Span<const char> text, size_t limit)
{ return text.length() <= limit && isValidUtf8(text); }
}

KiottySpecStatus SpecParser::parse(const KispFields& fields)
{
    const std::string_view table = viewOf(fields.values[0]);
    if (table == TABLE_MARKER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const std::string_view state = fields.count > 1 ? viewOf(fields.values[1]) : std::string_view();
    if (state != LIVE_MARKER && state != REMOVED_MARKER) { return invalid(_t.failure, table); }
    const uint32_t removed = state == REMOVED_MARKER ? 1u : 0u;
    if (table == META_TABLE) { return parseMeta(fields); }
    if (table == CHECKPOINT_TABLE) { return parseCheckpoint(fields); }
    if (table == ITEM_TABLE) { return parseItem(fields); }
    if (table == ITEM_VERSION_TABLE) { return parseItemVersion(fields, removed); }
    if (table == TAG_TABLE) { return parseTag(fields, removed); }
    if (table == RELATION_TABLE) { return parseRelation(fields, removed); }
    if (table == DECISION_TABLE) { return parseDecision(fields); }
    if (table == BODY_TABLE) { return parseBody(fields); }
    return invalid(_t.failure, table);
}

KiottySpecStatus SpecParser::parseMeta(const KispFields& fields)
{
    if (fields.count != META_FIELDS) { return invalid(_t.failure, META_TABLE); }
    const KiottySpecStatus set = _t.meta.set(fields.values[2], fields.values[3]);
    if (set != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(set, KIOTTY_SPEC_SUBJECT_FILE_LINE, spanOf(META_TABLE));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseCheckpoint(const KispFields& fields)
{
    if (fields.count != CHECKPOINT_FIELDS) { return invalid(_t.failure, CHECKPOINT_TABLE); }
    const auto sequence = parseSequence(fields.values[2]);
    const bool first = viewOf(fields.values[3]) == NONE_MARKER;
    const auto previous =
        first ? Result<uint32_t, KiottySpecStatus>::success(0) : parseSequence(fields.values[3]);
    const auto step = parseDecimal(fields.values[4], entity::STEP_COUNT);
    const uint32_t state = entity::parseState(viewOf(fields.values[5]));
    const auto created = parseDecimal(fields.values[6], MAX_EPOCH);
    const bool valid = sequence && previous && step &&
                       entity::isValidStep(static_cast<uint32_t>(step.value)) && state != 0 &&
                       created;
    if (!valid) { return invalid(_t.failure, CHECKPOINT_TABLE); }
    CheckpointRecord record {};
    record.created_at = static_cast<int64_t>(created.value);
    record.previous = CheckpointHandle{previous.value, 0};
    record.step = static_cast<entity::Step>(step.value);
    record.state = static_cast<entity::CheckpointState>(state);
    if (!fits(fields.values[7], entity::MAX_DESCRIPTION_LENGTH)) {
        return invalid(_t.failure, CHECKPOINT_TABLE);
    }
    record.description.assign(fields.values[7]);
    return _t.checkpoints.put(sequence.value, record);
}

KiottySpecStatus SpecParser::parseItem(const KispFields& fields)
{
    if (fields.count != ITEM_FIELDS) { return invalid(_t.failure, ITEM_TABLE); }
    const auto key = parseKey(fields.values[2]);
    if (!key || entity::validateItemId(fields.values[3]) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return invalid(_t.failure, ITEM_TABLE);
    }
    entity::IdText id {};
    id.assign(fields.values[3]);
    const KiottySpecStatus inserted = _t.items.tables().names.insert(key.value, id);
    if (inserted == KIOTTY_SPEC_STATUS_CONFLICT) {
        return _t.failure.raise(inserted, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE, fields.values[2]);
    }
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(inserted, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_items"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseItemVersion(const KispFields& fields, uint32_t removed)
{
    if (fields.count != ITEM_VERSION_FIELDS) { return invalid(_t.failure, ITEM_VERSION_TABLE); }
    const auto key = parseKey(fields.values[2]);
    const auto checkpoint = parseSequence(fields.values[3]);
    const auto kind = parseKindName(fields.values[4]);
    const auto parent = parseOptionalKey(fields.values[5]);
    const bool no_body = viewOf(fields.values[8]) == NONE_MARKER;
    const auto body = no_body ? Result<uint64_t, KiottySpecStatus>::success(0)
                              : parseHex(fields.values[8], HASH_DIGITS);
    if (!key || !checkpoint || !kind || !parent || !body) {
        return invalid(_t.failure, ITEM_VERSION_TABLE);
    }
    ItemVersion version {};
    version.body_hash = body.value;
    version.key = key.value;
    version.checkpoint = checkpoint.value;
    version.parent = parent.value;
    version.removed = removed;
    version.kind = kind.value;
    if (!fits(fields.values[6], entity::MAX_TITLE_LENGTH) ||
        !fits(fields.values[7], entity::MAX_SUMMARY_LENGTH)) {
        return invalid(_t.failure, ITEM_VERSION_TABLE);
    }
    version.title.assign(fields.values[6]);
    version.summary.assign(fields.values[7]);
    const KiottySpecStatus status = _t.items.tables().versions.append(version);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_items"));
    }
    _versions_sorted = false;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<ItemKind, KiottySpecStatus> SpecParser::kindOfKey(uint32_t key)
{
    if (!_versions_sorted) { _t.items.tables().versions.sort(); }
    _versions_sorted = true;
    const ItemVersion* version = _t.items.tables().versions.latest(key, UINT32_MAX);
    if (version == nullptr) {
        return Result<ItemKind, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemKind, KiottySpecStatus>::success(version->kind);
}

KiottySpecStatus SpecParser::parseTag(const KispFields& fields, uint32_t removed)
{
    if (fields.count != TAG_FIELDS) { return invalid(_t.failure, TAG_TABLE); }
    const auto key = parseKey(fields.values[2]);
    const auto checkpoint = parseSequence(fields.values[4]);
    const auto kind =
        key ? kindOfKey(key.value) : Result<ItemKind, KiottySpecStatus>::failure(key.code);
    if (!key || !checkpoint || !kind) { return invalid(_t.failure, TAG_TABLE); }
    const auto tag = _t.catalog.finder().findTag(kind.value, fields.values[3]);
    if (!tag) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG,
                                fields.values[3]);
    }
    const auto value = removed != 0 ? Result<uint64_t, KiottySpecStatus>::success(0)
                                    : parseTagValue(kind.value, tag.value, fields.values[5]);
    if (!value) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG_VALUE,
                                fields.values[5]);
    }
    const TagVersion version {value.value, key.value, tag.value, checkpoint.value, removed};
    const KiottySpecStatus appended = _t.items.tables().tag_versions.append(version);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(appended, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_tags"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<uint64_t, KiottySpecStatus> SpecParser::parseTagValue(ItemKind kind, uint32_t tag,
                                                             Span<const char> names) const
{
    uint64_t value = 0;
    size_t start = 0;
    for (size_t index = 0; index <= names.length(); ++index) {
        if (index < names.length() && names[index] != ',') { continue; }
        const auto bit = _t.catalog.finder().findTagBit(
            kind, tag, Span<const char>(names.data() + start, index - start));
        if (!bit) { return Result<uint64_t, KiottySpecStatus>::failure(bit.code); }
        value |= uint64_t{1} << bit.value;
        start = index + 1;
    }
    return Result<uint64_t, KiottySpecStatus>::success(value);
}

KiottySpecStatus SpecParser::parseRelation(const KispFields& fields, uint32_t removed)
{
    if (fields.count != RELATION_FIELDS) { return invalid(_t.failure, RELATION_TABLE); }
    const auto from = parseKey(fields.values[2]);
    const auto model = parseKindName(fields.values[3]);
    const auto to = parseKey(fields.values[5]);
    const auto checkpoint = parseSequence(fields.values[6]);
    if (!from || !model || !to || !checkpoint) { return invalid(_t.failure, RELATION_TABLE); }
    const auto relation = _t.catalog.finder().findRelation(model.value, fields.values[4]);
    if (!relation) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_RELATION,
                                fields.values[4]);
    }
    const RelationVersion version { from.value,       relation.value, to.value,
                                    checkpoint.value, removed,        model.value };
    const KiottySpecStatus appended = _t.relations.versions().append(version);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(appended, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_relations"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseDecision(const KispFields& fields)
{
    if (fields.count != DECISION_FIELDS) { return invalid(_t.failure, DECISION_TABLE); }
    const auto kind = parseKindName(fields.values[2]);
    const auto checkpoint = parseSequence(fields.values[3]);
    const bool valid = kind && checkpoint && fits(fields.values[4], MAX_DECISION_BYTES);
    if (!valid) { return invalid(_t.failure, DECISION_TABLE); }
    const auto slot =
        _t.decisions.versions().slot(static_cast<uint32_t>(kind.value), checkpoint.value);
    if (!slot) {
        return _t.failure.raise(slot.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_decision_versions"));
    }
    if (slot.value->text.length() > 0) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE,
                                fields.values[2]);
    }
    slot.value->kind = kind.value;
    slot.value->removed = 0;
    return slot.value->text.assign(fields.values[4]);
}

KiottySpecStatus SpecParser::parseBody(const KispFields& fields)
{
    if (fields.count != BODY_FIELDS) { return invalid(_t.failure, BODY_TABLE); }
    const auto key = parseKey(fields.values[2]);
    const bool valid =
        key && fields.values[3].length() > 0 && fits(fields.values[3], entity::MAX_BODY_LENGTH);
    if (!valid) { return invalid(_t.failure, BODY_TABLE); }
    ItemTables& tables = _t.items.tables();
    if (!tables.names.find(key.value)) { return invalid(_t.failure, BODY_TABLE); }
    if (tables.bodies.find(key.value).length() > 0) {
        return _t.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE,
                                fields.values[2]);
    }
    const KiottySpecStatus stored = tables.bodies.put(key.value, fields.values[3]);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _t.failure.raise(stored, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                literalText("max_body_bytes"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
