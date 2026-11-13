#include "implementation/data/repository/spec_file/kiotty_rule_spec_writer.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/spec_file/kiotty_spec_line.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;
using entity::ItemVersion;
using entity::TagVersion;

constexpr uint32_t EXPORTED_CHECKPOINT = 1;
constexpr std::string_view PROJECT_NAME_KEY = "project.name";
constexpr std::string_view LINE_END = "\n";

}

KiottySpecStatus RuleSpecWriter::write(uint32_t checkpoint)
{
    KiottySpecStatus status = _file.write(spanOf(KISP_HEADER));
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _file.write(spanOf(LINE_END)); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeMeta(); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeCheckpoint(checkpoint); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeItems(checkpoint); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeItemVersions(checkpoint); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeTags(checkpoint); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _file.write(spanOf(RELATION_HEADER)); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _file.write(spanOf(DECISION_HEADER)); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = writeBodies(checkpoint); }
    return status;
}

KiottySpecStatus RuleSpecWriter::writeMeta()
{
    const KiottySpecStatus status = _file.write(spanOf(META_HEADER));
    const auto name = _tables.meta.get(spanOf(PROJECT_NAME_KEY));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || !name) { return status; }
    SpecLine line(KispLineEncoder(_area), META_TABLE, 0);
    line.add(spanOf(PROJECT_NAME_KEY));
    line.add(name.value.view());
    return emit(line.finish());
}

KiottySpecStatus RuleSpecWriter::writeCheckpoint(uint32_t checkpoint)
{
    const KiottySpecStatus status = _file.write(spanOf(CHECKPOINT_HEADER));
    const auto found = _tables.checkpoints.get(checkpoint);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || !found) { return status; }
    const entity::CheckpointRecord& record = *found.value;
    SpecLine line(KispLineEncoder(_area), CHECKPOINT_TABLE, 0);
    line.addDecimal(EXPORTED_CHECKPOINT);
    line.add(spanOf(NONE_MARKER));
    line.addDecimal(static_cast<uint64_t>(entity::Step::REQUIREMENT_DISCUSSING));
    line.add(spanOf(entity::stateName(entity::CheckpointState::DONE)));
    line.addDecimal(record.created_at > 0 ? static_cast<uint64_t>(record.created_at) : 0);
    const entity::DescriptionText description = _tables.checkpoints.describe(checkpoint);
    line.add(description.view());
    return emit(line.finish());
}

KiottySpecStatus RuleSpecWriter::writeItems(uint32_t checkpoint)
{
    KiottySpecStatus status = _file.write(spanOf(ITEM_HEADER));
    const FixedMap<uint32_t, entity::IdText>& names = _tables.items.tables().names;
    const size_t name_count = names.length();
    for (size_t index = 0; index < name_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        const uint32_t key = names.keyAt(index);
        const bool is_rule = isRuleAt(key, checkpoint);
        if (!is_rule) { continue; }
        SpecLine line(KispLineEncoder(_area), ITEM_TABLE, 0);
        line.addKey(key);
        line.add(names.valueAt(index).view());
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus RuleSpecWriter::writeItemVersions(uint32_t checkpoint)
{
    KiottySpecStatus status = _file.write(spanOf(ITEM_VERSION_HEADER));
    const ItemTables& tables = _tables.items.tables();
    const size_t name_count = tables.names.length();
    for (size_t index = 0; index < name_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        const uint32_t key = tables.names.keyAt(index);
        const auto latest = tables.versions.latest(key, checkpoint);
        const bool is_rule = isRuleAt(key, checkpoint);
        if (!is_rule || !latest) { continue; }
        const ItemVersion& version = *latest.value;
        SpecLine line(KispLineEncoder(_area), ITEM_VERSION_TABLE, 0);
        line.addKey(key);
        line.addDecimal(EXPORTED_CHECKPOINT);
        line.add(spanOf(entity::kindName(ItemKind::RULE)));
        line.addKey(version.parent.index);
        const entity::ItemVersionLabel& label = tables.version_labels.read(version.label);
        line.add(label.title.view());
        line.add(label.summary.view());
        line.addHash(version.body_hash);
        status = emit(line.finish());
    }
    return status;
}

KiottySpecStatus RuleSpecWriter::writeTags(uint32_t checkpoint)
{
    KiottySpecStatus status = _file.write(spanOf(TAG_HEADER));
    for (const TagVersion& version : _tables.items.tables().tag_versions.all()) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const bool is_current = isCurrentTag(version, checkpoint);
        if (!is_current) { continue; }
        status = writeTag(version);
    }
    return status;
}

KiottySpecStatus RuleSpecWriter::writeTag(const TagVersion& version)
{
    const Span<const entity::TagDefinition> tags = _tables.catalog.finder().tagsOf(ItemKind::RULE);
    const auto definition = tags.at(version.tag);
    if (!definition) { return _tables.failure.raise(definition.code, KIOTTY_SPEC_SUBJECT_TAG); }
    SpecLine line(KispLineEncoder(_area), TAG_TABLE, 0);
    line.addKey(version.item.index);
    line.add((*definition.value).name.view());
    line.addDecimal(EXPORTED_CHECKPOINT);
    line.addTagValue(_tables.catalog.finder().bitNamesOf(ItemKind::RULE, version.tag),
                     version.value);
    return emit(line.finish());
}

KiottySpecStatus RuleSpecWriter::writeBodies(uint32_t checkpoint)
{
    KiottySpecStatus status = _file.write(spanOf(BODY_HEADER));
    const BodyStore& bodies = _tables.items.tables().bodies;
    const size_t body_count = bodies.length();
    for (size_t index = 0; index < body_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        const uint32_t key = bodies.keyAt(index);
        const bool is_rule = isRuleAt(key, checkpoint);
        if (!is_rule) { continue; }
        SpecLine line(KispLineEncoder(_area), BODY_TABLE, 0);
        line.addKey(key);
        line.add(bodies.bodyAt(index));
        status = emit(line.finish());
    }
    return status;
}

bool RuleSpecWriter::isRuleAt(uint32_t key, uint32_t checkpoint) const
{
    const auto version = _tables.items.tables().versions.latest(key, checkpoint);
    if (!version) { return false; }
    const ItemVersion& found = *version.value;
    return found.kind == ItemKind::RULE && found.removed == 0;
}

bool RuleSpecWriter::isCurrentTag(const TagVersion& version, uint32_t checkpoint) const
{
    const bool is_rule = isRuleAt(version.item.index, checkpoint);
    const auto latest = _tables.items.tables().tag_versions.latest(version.identity(), checkpoint);
    if (!is_rule || !latest) { return false; }
    const bool is_latest = &*latest.value == &version;
    return is_latest && version.removed == 0 && version.value != 0;
}

KiottySpecStatus RuleSpecWriter::emit(const Result<Span<const char>, KiottySpecStatus>& finished)
{
    if (!finished) {
        return _tables.failure.raise(finished.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                     literalText("work_buffer_bytes"));
    }
    return _file.write(finished.value);
}

}
