#include "implementation/domain/usecases/spec_file/kiotty_spec_writer.h"

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

class SpecLine
{
public:
    SpecLine(KispLineEncoder line, std::string_view table, uint32_t removed);

    void add(Span<const char> field) { _line.add(field); }
    void addDecimal(uint64_t number);
    void addKey(uint32_t key);
    void addHash(uint64_t hash);
    void addTagValue(Span<const entity::TagName> bit_names, uint64_t value);
    Result<Span<const char>, KiottySpecStatus> finish() { return _line.finish(); }

private:
    KispLineEncoder _line;
};

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
    const FixedMap<MetaKey, MetaValue>& entries = _tables.meta.all();
    const size_t entry_count = entries.length();
    for (size_t index = 0; index < entry_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        SpecLine line(KispLineEncoder(_area), META_TABLE, 0);
        line.add(entries.keyAt(index).view());
        line.add(entries.valueAt(index).view());
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeCheckpoints()
{
    KiottySpecStatus status = writeHeader(CHECKPOINT_HEADER);
    const uint32_t latest = _tables.checkpoints.latest();
    for (uint32_t sequence = 1; sequence <= latest && status == KIOTTY_SPEC_STATUS_SUCCESS;
         ++sequence) {
        const auto found = _tables.checkpoints.get(sequence);
        if (!found) { continue; }
        const CheckpointRecord& record = *found.value;
        SpecLine line(KispLineEncoder(_area), CHECKPOINT_TABLE, 0);
        line.addDecimal(sequence);
        const bool has_previous = record.previous.isValid();
        if (has_previous) {
            line.addDecimal(record.previous.sequence);
        } else {
            line.add(spanOf(NONE_MARKER));
        }
        line.addDecimal(static_cast<uint64_t>(record.step));
        line.add(spanOf(entity::stateName(record.state)));
        line.addDecimal(record.created_at > 0 ? static_cast<uint64_t>(record.created_at) : 0);
        const entity::DescriptionText description = _tables.checkpoints.describe(sequence);
        line.add(description.view());
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeItems()
{
    KiottySpecStatus status = writeHeader(ITEM_HEADER);
    const FixedMap<uint32_t, entity::IdText>& names = _tables.items.tables().names;
    const size_t name_count = names.length();
    for (size_t index = 0; index < name_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        SpecLine line(KispLineEncoder(_area), ITEM_TABLE, 0);
        line.addKey(names.keyAt(index));
        line.add(names.valueAt(index).view());
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeItemVersions()
{
    KiottySpecStatus status = writeHeader(ITEM_VERSION_HEADER);
    for (const ItemVersion& version : _tables.items.tables().versions.all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        SpecLine line(KispLineEncoder(_area), ITEM_VERSION_TABLE, version.removed);
        line.addKey(version.key);
        line.addDecimal(version.checkpoint);
        line.add(spanOf(entity::kindName(version.kind)));
        line.addKey(version.parent.index);
        const entity::ItemVersionLabel& label =
            _tables.items.tables().version_labels.read(version.label);
        line.add(label.title.view());
        line.add(label.summary.view());
        line.addHash(version.body_hash);
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeTags()
{
    KiottySpecStatus status = writeHeader(TAG_HEADER);
    const ItemTables& tables = _tables.items.tables();
    for (const TagVersion& version : tables.tag_versions.all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const auto item = tables.versions.latest(version.item.index, UINT32_MAX);
        if (!item) { continue; }
        const ItemKind kind = (*item.value).kind;
        const Span<const entity::TagDefinition> tags = _tables.catalog.finder().tagsOf(kind);
        const auto found = tags.at(version.tag);
        if (!found) { return _tables.failure.raise(found.code, KIOTTY_SPEC_SUBJECT_TAG); }
        const entity::TagDefinition& definition = *found.value;
        SpecLine line(KispLineEncoder(_area), TAG_TABLE, version.removed);
        line.addKey(version.item.index);
        line.add(definition.name.view());
        line.addDecimal(version.checkpoint);
        const uint64_t value = version.removed != 0 ? 0 : version.value;
        line.addTagValue(_tables.catalog.finder().bitNamesOf(kind, version.tag), value);
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeRelations()
{
    KiottySpecStatus status = writeHeader(RELATION_HEADER);
    for (const RelationVersion& version : _tables.relations.versions().all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const Span<const entity::RelationDefinition> relations =
            _tables.catalog.finder().relationsOf(version.model);
        const auto found = relations.at(version.relation);
        if (!found) { return _tables.failure.raise(found.code, KIOTTY_SPEC_SUBJECT_RELATION); }
        const entity::RelationDefinition& definition = *found.value;
        SpecLine line(KispLineEncoder(_area), RELATION_TABLE, version.removed);
        line.addKey(version.from.index);
        line.add(spanOf(entity::kindName(version.model)));
        line.add(definition.name.view());
        line.addKey(version.to.index);
        line.addDecimal(version.checkpoint);
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeDecisions()
{
    KiottySpecStatus status = writeHeader(DECISION_HEADER);
    for (const DecisionVersion& version : _tables.decisions.versions().all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        SpecLine line(KispLineEncoder(_area), DECISION_TABLE, 0);
        line.add(spanOf(entity::kindName(version.kind)));
        line.addDecimal(version.checkpoint);
        line.add(_tables.decisions.textOf(version).view());
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeBodies()
{
    KiottySpecStatus status = writeHeader(BODY_HEADER);
    const BodyStore& bodies = _tables.items.tables().bodies;
    const size_t body_count = bodies.length();
    for (size_t index = 0; index < body_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        SpecLine line(KispLineEncoder(_area), BODY_TABLE, 0);
        line.addKey(bodies.keyAt(index));
        line.add(bodies.bodyAt(index));
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus SpecWriter::writeHeader(std::string_view columns)
{ return _file.write(spanOf(columns)); }

KiottySpecStatus SpecWriter::emit(const Result<Span<const char>, KiottySpecStatus>& finished)
{
    if (!finished) {
        return _tables.failure.raise(finished.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("work_buffer_bytes"));
    }
    return _file.write(finished.value);
}

namespace {

SpecLine::SpecLine(KispLineEncoder line, std::string_view table, uint32_t removed) : _line(line)
{
    _line.add(spanOf(table));
    _line.add(spanOf(removed != 0 ? REMOVED_MARKER : LIVE_MARKER));
}

void SpecLine::addDecimal(uint64_t number)
{
    const DecimalText text = formatDecimal(number);
    _line.add(text.view());
}

void SpecLine::addKey(uint32_t key)
{
    if (key == entity::NO_KEY) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    const HexText text = formatHex(key, KEY_DIGITS);
    _line.add(text.view());
}

void SpecLine::addHash(uint64_t hash)
{
    if (hash == 0) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    const HexText text = formatHex(hash, HASH_DIGITS);
    _line.add(text.view());
}

void SpecLine::addTagValue(Span<const entity::TagName> bit_names, uint64_t value)
{
    if (value == 0) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    _line.add(Span<const char>());
    bool has_written_bit = false;
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        if (has_written_bit) { _line.extend(literalText(",")); }
        _line.extend(bit_names[bit].view());
        has_written_bit = true;
    }
}

}

}
