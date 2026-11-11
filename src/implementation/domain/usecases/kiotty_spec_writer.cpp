#include "implementation/domain/usecases/kiotty_spec_writer.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::DecisionVersion;
using entity::ItemKind;
using entity::ItemVersion;
using entity::RelationVersion;
using entity::TagVersion;

constexpr std::string_view META_HEADER = "TABLE\tmeta\tO/X\tkey\tvalue\n";
constexpr std::string_view CHECKPOINT_HEADER =
    "TABLE\tcheckpoint\tO/X\tsequence\tprevious\tstep\tstate\tcreated_at\tdescription\n";
constexpr std::string_view ITEM_HEADER = "TABLE\titem\tO/X\tkey\tid\n";
constexpr std::string_view ITEM_VERSION_HEADER =
    "TABLE\titem_version\tO/X\tkey\tcheckpoint\tkind\tparent\ttitle\tsummary\tbody\n";
constexpr std::string_view TAG_HEADER = "TABLE\ttag\tO/X\tkey\ttag\tcheckpoint\tvalue\n";
constexpr std::string_view RELATION_HEADER =
    "TABLE\trelation\tO/X\tfrom\tmodel\trelation\tto\tcheckpoint\n";
constexpr std::string_view DECISION_HEADER = "TABLE\tdecision\tO/X\tkind\tcheckpoint\ttext\n";
constexpr std::string_view BODY_HEADER = "TABLE\tbody\tO/X\tkey\ttext\n";

constexpr size_t KEY_DIGITS = 8;
constexpr size_t HASH_DIGITS = 16;
constexpr size_t MAX_NUMBER_DIGITS = 20;
using NumberText = FixedString<MAX_NUMBER_DIGITS>;

void addDecimal(KispLineEncoder& line, uint64_t number);
void addKey(KispLineEncoder& line, uint32_t key);
void addHash(KispLineEncoder& line, uint64_t hash);
void addTagValue(KispLineEncoder& line, const entity::TagDefinition& definition, uint64_t value);

}

KiottySpecStatus SpecWriter::write()
{
    KiottySpecStatus status = _file.write(spanOf(KISP_HEADER));
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _file.write(literalText("\n")); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeMeta(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeCheckpoints(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeItems(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeItemVersions(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeTags(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeRelations(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeDecisions(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeBodies(); }
    return status;
}

KiottySpecStatus SpecWriter::writeMeta()
{
    KiottySpecStatus status = writeHeader(META_HEADER);
    const auto& entries = _tables.meta.all();
    const size_t entry_count = entries.length();
    for (size_t index = 0; index < entry_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        KispLineEncoder line = startLine(META_TABLE, 0);
        line.add(entries.keyAt(index).view());
        line.add(entries.valueAt(index).view());
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeCheckpoints()
{
    KiottySpecStatus status = writeHeader(CHECKPOINT_HEADER);
    const uint32_t latest = _tables.checkpoints.latest();
    for (uint32_t sequence = 1; sequence <= latest && status == KIOTTY_SPEC_STATUS_SUCCESS;
         ++sequence) {
        const CheckpointRecord* record = _tables.checkpoints.at(sequence);
        if (record == nullptr) { continue; }
        KispLineEncoder line = startLine(CHECKPOINT_TABLE, 0);
        addDecimal(line, sequence);
        const bool has_previous = record->previous.isValid();
        if (has_previous) {
            addDecimal(line, record->previous.sequence);
        } else {
            line.add(spanOf(NONE_MARKER));
        }
        addDecimal(line, static_cast<uint64_t>(record->step));
        line.add(spanOf(entity::stateName(record->state)));
        addDecimal(line, record->created_at > 0 ? static_cast<uint64_t>(record->created_at) : 0);
        line.add(record->description.view());
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeItems()
{
    KiottySpecStatus status = writeHeader(ITEM_HEADER);
    const auto& names = _tables.items.tables().names;
    const size_t name_count = names.length();
    for (size_t index = 0; index < name_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        KispLineEncoder line = startLine(ITEM_TABLE, 0);
        addKey(line, names.keyAt(index));
        line.add(names.valueAt(index).view());
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeItemVersions()
{
    KiottySpecStatus status = writeHeader(ITEM_VERSION_HEADER);
    for (const ItemVersion& version : _tables.items.tables().versions.all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        KispLineEncoder line = startLine(ITEM_VERSION_TABLE, version.removed);
        addKey(line, version.key);
        addDecimal(line, version.checkpoint);
        line.add(spanOf(entity::kindName(version.kind)));
        addKey(line, version.parent);
        line.add(version.title.view());
        line.add(version.summary.view());
        addHash(line, version.body_hash);
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeTags()
{
    KiottySpecStatus status = writeHeader(TAG_HEADER);
    const ItemTables& tables = _tables.items.tables();
    for (const TagVersion& version : tables.tag_versions.all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const ItemVersion* item = tables.versions.latest(version.key, UINT32_MAX);
        if (item == nullptr) { continue; }
        const Span<const entity::TagDefinition> tags = _tables.catalog.model(item->kind).tags;
        const auto found = tags.at(version.tag);
        if (!found) { return _tables.failure.raise(found.code, KIOTTY_SPEC_SUBJECT_TAG); }
        const entity::TagDefinition& definition = *found.value;
        KispLineEncoder line = startLine(TAG_TABLE, version.removed);
        addKey(line, version.key);
        line.add(definition.name.view());
        addDecimal(line, version.checkpoint);
        addTagValue(line, definition, version.removed != 0 ? 0 : version.value);
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeRelations()
{
    KiottySpecStatus status = writeHeader(RELATION_HEADER);
    for (const RelationVersion& version : _tables.relations.versions().all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const Span<const entity::RelationDefinition> relations =
            _tables.catalog.model(version.model).relations;
        const auto found = relations.at(version.relation);
        if (!found) { return _tables.failure.raise(found.code, KIOTTY_SPEC_SUBJECT_RELATION); }
        const entity::RelationDefinition& definition = *found.value;
        KispLineEncoder line = startLine(RELATION_TABLE, version.removed);
        addKey(line, version.from);
        line.add(spanOf(entity::kindName(version.model)));
        line.add(definition.name.view());
        addKey(line, version.to);
        addDecimal(line, version.checkpoint);
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeDecisions()
{
    KiottySpecStatus status = writeHeader(DECISION_HEADER);
    for (const DecisionVersion& version : _tables.decisions.versions().all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        KispLineEncoder line = startLine(DECISION_TABLE, 0);
        line.add(spanOf(entity::kindName(version.kind)));
        addDecimal(line, version.checkpoint);
        line.add(version.text.view());
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeBodies()
{
    KiottySpecStatus status = writeHeader(BODY_HEADER);
    const BodyStore& bodies = _tables.items.tables().bodies;
    const size_t body_count = bodies.length();
    for (size_t index = 0; index < body_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        KispLineEncoder line = startLine(BODY_TABLE, 0);
        addKey(line, bodies.keyAt(index));
        line.add(bodies.bodyAt(index));
        status = emit(line);
    }
    return status;
}

KiottySpecStatus SpecWriter::writeHeader(std::string_view columns)
{ return _file.write(spanOf(columns)); }

KispLineEncoder SpecWriter::startLine(std::string_view table, uint32_t removed)
{
    KispLineEncoder line(_area);
    line.add(spanOf(table));
    line.add(spanOf(removed != 0 ? REMOVED_MARKER : LIVE_MARKER));
    return line;
}

KiottySpecStatus SpecWriter::emit(KispLineEncoder& line)
{
    const auto finished = line.finish();
    if (!finished) {
        return _tables.failure.raise(finished.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("work_buffer_bytes"));
    }
    return _file.write(finished.value);
}

namespace {

void addDecimal(KispLineEncoder& line, uint64_t number)
{
    NumberText text {};
    appendDecimal(text, number);
    line.add(text.view());
}

void addKey(KispLineEncoder& line, uint32_t key)
{
    if (key == entity::NO_KEY) {
        line.add(spanOf(NONE_MARKER));
        return;
    }
    NumberText text {};
    appendHex(text, key, KEY_DIGITS);
    line.add(text.view());
}

void addHash(KispLineEncoder& line, uint64_t hash)
{
    if (hash == 0) {
        line.add(spanOf(NONE_MARKER));
        return;
    }
    NumberText text {};
    appendHex(text, hash, HASH_DIGITS);
    line.add(text.view());
}

void addTagValue(KispLineEncoder& line, const entity::TagDefinition& definition, uint64_t value)
{
    if (value == 0) {
        line.add(spanOf(NONE_MARKER));
        return;
    }
    line.add(Span<const char>());
    bool has_written_bit = false;
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        if (has_written_bit) { line.extend(literalText(",")); }
        line.extend(definition.bit_names[bit].view());
        has_written_bit = true;
    }
}

}

}
