#include "implementation/domain/usecases/kiotty_spec_parser.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/entity/kiotty_item_id.h"
#include "implementation/domain/usecases/spec_parser/kiotty_spec_fields.h"

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

constexpr size_t TABLE_FIELD = 0;
constexpr size_t STATE_FIELD = 1;

constexpr size_t META_KEY_FIELD = 2;
constexpr size_t META_VALUE_FIELD = 3;

constexpr size_t CHECKPOINT_SEQUENCE_FIELD = 2;
constexpr size_t CHECKPOINT_PREVIOUS_FIELD = 3;
constexpr size_t CHECKPOINT_STEP_FIELD = 4;
constexpr size_t CHECKPOINT_STATE_FIELD = 5;
constexpr size_t CHECKPOINT_CREATED_AT_FIELD = 6;
constexpr size_t CHECKPOINT_DESCRIPTION_FIELD = 7;

constexpr size_t ITEM_KEY_FIELD = 2;
constexpr size_t ITEM_ID_FIELD = 3;

constexpr size_t ITEM_VERSION_KEY_FIELD = 2;
constexpr size_t ITEM_VERSION_CHECKPOINT_FIELD = 3;
constexpr size_t ITEM_VERSION_KIND_FIELD = 4;
constexpr size_t ITEM_VERSION_PARENT_FIELD = 5;
constexpr size_t ITEM_VERSION_TITLE_FIELD = 6;
constexpr size_t ITEM_VERSION_SUMMARY_FIELD = 7;
constexpr size_t ITEM_VERSION_BODY_FIELD = 8;

constexpr size_t TAG_KEY_FIELD = 2;
constexpr size_t TAG_NAME_FIELD = 3;
constexpr size_t TAG_CHECKPOINT_FIELD = 4;
constexpr size_t TAG_VALUE_FIELD = 5;

constexpr size_t RELATION_FROM_FIELD = 2;
constexpr size_t RELATION_MODEL_FIELD = 3;
constexpr size_t RELATION_NAME_FIELD = 4;
constexpr size_t RELATION_TO_FIELD = 5;
constexpr size_t RELATION_CHECKPOINT_FIELD = 6;

constexpr size_t DECISION_KIND_FIELD = 2;
constexpr size_t DECISION_CHECKPOINT_FIELD = 3;
constexpr size_t DECISION_TEXT_FIELD = 4;

constexpr size_t BODY_KEY_FIELD = 2;
constexpr size_t BODY_TEXT_FIELD = 3;

constexpr uint64_t MAX_EPOCH = INT64_MAX;

}

KiottySpecStatus SpecParser::parse(const KispFields& fields)
{
    const std::string_view table = viewField(fields, TABLE_FIELD);
    if (table == TABLE_MARKER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const std::string_view state = viewField(fields, STATE_FIELD);
    if (state != LIVE_MARKER && state != REMOVED_MARKER) { return reject(table); }
    const uint32_t removed = state == REMOVED_MARKER ? 1u : 0u;
    if (table == META_TABLE) { return parseMeta(fields); }
    if (table == CHECKPOINT_TABLE) { return parseCheckpoint(fields); }
    if (table == ITEM_TABLE) { return parseItem(fields); }
    if (table == ITEM_VERSION_TABLE) { return parseItemVersion(fields, removed); }
    if (table == TAG_TABLE) { return parseTag(fields, removed); }
    if (table == RELATION_TABLE) { return parseRelation(fields, removed); }
    if (table == DECISION_TABLE) { return parseDecision(fields); }
    if (table == BODY_TABLE) { return parseBody(fields); }
    return reject(table);
}

KiottySpecStatus SpecParser::parseMeta(const KispFields& fields)
{
    if (fields.count != META_FIELDS) { return reject(META_TABLE); }
    const auto key = readField(fields, META_KEY_FIELD);
    const auto value = readField(fields, META_VALUE_FIELD);
    const bool has_fields = key && value;
    if (!has_fields) { return reject(META_TABLE); }
    const KiottySpecStatus set = _tables.meta.set(key.value, value.value);
    if (set != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(set, KIOTTY_SPEC_SUBJECT_FILE_LINE, spanOf(META_TABLE));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseCheckpoint(const KispFields& fields)
{
    if (fields.count != CHECKPOINT_FIELDS) { return reject(CHECKPOINT_TABLE); }
    const auto sequence = parseSequence(fields, CHECKPOINT_SEQUENCE_FIELD);
    const bool is_first = viewField(fields, CHECKPOINT_PREVIOUS_FIELD) == NONE_MARKER;
    const auto previous = is_first ? KeyResult::success(0)
                                   : parseSequence(fields, CHECKPOINT_PREVIOUS_FIELD);
    const auto step = parseNumber(fields, CHECKPOINT_STEP_FIELD, entity::STEP_COUNT);
    const uint32_t state = entity::parseState(viewField(fields, CHECKPOINT_STATE_FIELD));
    const auto created = parseNumber(fields, CHECKPOINT_CREATED_AT_FIELD, MAX_EPOCH);
    const auto description =
        readText(fields, CHECKPOINT_DESCRIPTION_FIELD, entity::MAX_DESCRIPTION_LENGTH);
    const bool is_valid = sequence && previous && step &&
                          entity::isValidStep(static_cast<uint32_t>(step.value)) && state != 0 &&
                          created && description;
    if (!is_valid) { return reject(CHECKPOINT_TABLE); }
    CheckpointRecord record {};
    record.created_at = static_cast<int64_t>(created.value);
    record.previous = CheckpointHandle{previous.value, 0};
    record.step = static_cast<entity::Step>(step.value);
    record.state = static_cast<entity::CheckpointState>(state);
    record.description.assign(description.value);
    return _tables.checkpoints.put(sequence.value, record);
}

KiottySpecStatus SpecParser::parseItem(const KispFields& fields)
{
    if (fields.count != ITEM_FIELDS) { return reject(ITEM_TABLE); }
    const auto key_text = readField(fields, ITEM_KEY_FIELD);
    const auto key = parseKey(fields, ITEM_KEY_FIELD);
    const auto id_text = readField(fields, ITEM_ID_FIELD);
    const bool has_fields = key_text && key && id_text;
    if (!has_fields) { return reject(ITEM_TABLE); }
    const KiottySpecStatus id_status = entity::validateItemId(id_text.value);
    if (id_status != KIOTTY_SPEC_STATUS_SUCCESS) { return reject(ITEM_TABLE); }
    entity::IdText id {};
    id.assign(id_text.value);
    const KiottySpecStatus inserted = _tables.items.tables().names.insert(key.value, id);
    if (inserted == KIOTTY_SPEC_STATUS_CONFLICT) {
        return _tables.failure.raise(inserted, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE, key_text.value);
    }
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(inserted, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_items"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseItemVersion(const KispFields& fields, uint32_t removed)
{
    if (fields.count != ITEM_VERSION_FIELDS) { return reject(ITEM_VERSION_TABLE); }
    const auto key = parseKey(fields, ITEM_VERSION_KEY_FIELD);
    const auto checkpoint = parseSequence(fields, ITEM_VERSION_CHECKPOINT_FIELD);
    const auto kind = parseKindName(fields, ITEM_VERSION_KIND_FIELD);
    const auto parent = parseOptionalKey(fields, ITEM_VERSION_PARENT_FIELD);
    const auto body = parseHash(fields, ITEM_VERSION_BODY_FIELD);
    const bool is_valid = key && checkpoint && kind && parent && body;
    if (!is_valid) { return reject(ITEM_VERSION_TABLE); }
    const auto title = readText(fields, ITEM_VERSION_TITLE_FIELD, entity::MAX_TITLE_LENGTH);
    const auto summary = readText(fields, ITEM_VERSION_SUMMARY_FIELD, entity::MAX_SUMMARY_LENGTH);
    const bool has_texts = title && summary;
    if (!has_texts) { return reject(ITEM_VERSION_TABLE); }
    ItemVersion version {};
    version.body_hash = body.value;
    version.key = key.value;
    version.checkpoint = checkpoint.value;
    version.parent = parent.value;
    version.removed = removed;
    version.kind = kind.value;
    version.title.assign(title.value);
    version.summary.assign(summary.value);
    const KiottySpecStatus status = _tables.items.tables().versions.append(version);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_items"));
    }
    _has_sorted_versions = false;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseTag(const KispFields& fields, uint32_t removed)
{
    if (fields.count != TAG_FIELDS) { return reject(TAG_TABLE); }
    const auto key = parseKey(fields, TAG_KEY_FIELD);
    const auto checkpoint = parseSequence(fields, TAG_CHECKPOINT_FIELD);
    const auto name = readField(fields, TAG_NAME_FIELD);
    const auto names = readField(fields, TAG_VALUE_FIELD);
    const auto kind =
        key ? kindOfKey(key.value) : Result<ItemKind, KiottySpecStatus>::failure(key.code);
    const bool is_valid = key && checkpoint && kind && name && names;
    if (!is_valid) { return reject(TAG_TABLE); }
    const auto tag = _tables.catalog.finder().findTag(kind.value, name.value);
    if (!tag) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG,
                                     name.value);
    }
    const auto value = removed != 0 ? NumberResult::success(0)
                                    : parseTagValue(kind.value, tag.value, names.value);
    if (!value) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                     KIOTTY_SPEC_SUBJECT_TAG_VALUE, names.value);
    }
    const TagVersion version {value.value, key.value, tag.value, checkpoint.value, removed};
    const KiottySpecStatus appended = _tables.items.tables().tag_versions.append(version);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(appended, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_tags"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<ItemKind, KiottySpecStatus> SpecParser::kindOfKey(uint32_t key)
{
    VersionTable<ItemVersion>& versions = _tables.items.tables().versions;
    const bool is_sorted = _has_sorted_versions;
    if (!is_sorted) { versions.sort(); }
    _has_sorted_versions = true;
    const auto version = versions.latest(key, UINT32_MAX);
    if (!version) { return Result<ItemKind, KiottySpecStatus>::failure(version.code); }
    return Result<ItemKind, KiottySpecStatus>::success((*version.value).kind);
}

Result<uint64_t, KiottySpecStatus> SpecParser::parseTagValue(ItemKind kind, uint32_t tag,
                                                             Span<const char> names) const
{
    uint64_t value = 0;
    size_t start = 0;
    const size_t length = names.length();
    for (size_t index = 0; index <= length; ++index) {
        if (index < length && names[index] != ',') { continue; }
        const Result<Span<const char>, KiottySpecStatus> name =
            names.subspan(start, index - start);
        const Result<uint32_t, KiottySpecStatus> bit =
            _tables.catalog.finder().findTagBit(kind, tag, name.value);
        if (!bit) { return Result<uint64_t, KiottySpecStatus>::failure(bit.code); }
        value |= uint64_t{1} << bit.value;
        start = index + 1;
    }
    return Result<uint64_t, KiottySpecStatus>::success(value);
}

KiottySpecStatus SpecParser::parseRelation(const KispFields& fields, uint32_t removed)
{
    if (fields.count != RELATION_FIELDS) { return reject(RELATION_TABLE); }
    const auto from = parseKey(fields, RELATION_FROM_FIELD);
    const auto model = parseKindName(fields, RELATION_MODEL_FIELD);
    const auto to = parseKey(fields, RELATION_TO_FIELD);
    const auto checkpoint = parseSequence(fields, RELATION_CHECKPOINT_FIELD);
    const auto name = readField(fields, RELATION_NAME_FIELD);
    const bool is_valid = from && model && to && checkpoint && name;
    if (!is_valid) { return reject(RELATION_TABLE); }
    const auto relation = _tables.catalog.finder().findRelation(model.value, name.value);
    if (!relation) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                     KIOTTY_SPEC_SUBJECT_RELATION, name.value);
    }
    const RelationVersion version { from.value,       relation.value, to.value,
                                    checkpoint.value, removed,        model.value };
    const KiottySpecStatus appended = _tables.relations.versions().append(version);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(appended, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_relations"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::parseDecision(const KispFields& fields)
{
    if (fields.count != DECISION_FIELDS) { return reject(DECISION_TABLE); }
    const auto kind_text = readField(fields, DECISION_KIND_FIELD);
    const auto kind = parseKindName(fields, DECISION_KIND_FIELD);
    const auto checkpoint = parseSequence(fields, DECISION_CHECKPOINT_FIELD);
    const auto text = readText(fields, DECISION_TEXT_FIELD, MAX_DECISION_BYTES);
    const bool is_valid = kind_text && kind && checkpoint && text;
    if (!is_valid) { return reject(DECISION_TABLE); }
    const auto slot =
        _tables.decisions.versions().slot(static_cast<uint32_t>(kind.value), checkpoint.value);
    if (!slot) {
        return _tables.failure.raise(slot.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_decision_versions"));
    }
    entity::DecisionVersion& version = *slot.value;
    const size_t existing_length = version.text.length();
    if (existing_length > 0) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT,
                                     KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE, kind_text.value);
    }
    version.kind = kind.value;
    version.removed = 0;
    return version.text.assign(text.value);
}

KiottySpecStatus SpecParser::parseBody(const KispFields& fields)
{
    if (fields.count != BODY_FIELDS) { return reject(BODY_TABLE); }
    const auto key_text = readField(fields, BODY_KEY_FIELD);
    const auto key = parseKey(fields, BODY_KEY_FIELD);
    const auto text = readText(fields, BODY_TEXT_FIELD, entity::MAX_BODY_LENGTH);
    const bool is_valid = key_text && key && text && text.value.length() > 0;
    if (!is_valid) { return reject(BODY_TABLE); }
    ItemTables& tables = _tables.items.tables();
    const auto known = tables.names.find(key.value);
    if (!known) { return reject(BODY_TABLE); }
    const size_t existing_length = tables.bodies.find(key.value).length();
    if (existing_length > 0) {
        return _tables.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT,
                                     KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE, key_text.value);
    }
    const KiottySpecStatus stored = tables.bodies.put(key.value, text.value);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _tables.failure.raise(stored, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("max_body_bytes"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus SpecParser::reject(std::string_view table)
{
    return _tables.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                 KIOTTY_SPEC_SUBJECT_FILE_LINE, spanOf(table));
}

}
