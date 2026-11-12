#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>
#include <memory>

namespace kiotty::cli {

namespace {

struct TagSpec {
    TagColumn tag {};
    std::optional<uint64_t> mask {};
};

std::optional<ColumnPlan> planTagColumns(const Session& session, KiottySpecItemKind kind);
std::optional<TagSpec> parseTagSpec(const Session& session, KiottySpecItemKind kind,
                                    const std::string& spec);
std::vector<TagColumn> readAllTags(const Session& session, KiottySpecItemKind kind);
bool hasTagColumn(const std::vector<Column>& columns, uint32_t index);
Column makeTagColumn(const TagColumn& tag);
void printTagQueryLimit();
std::optional<std::vector<uint32_t>> chooseRelations(const Session& session,
                                                     KiottySpecItemKind kind);
std::optional<Column> readRelationColumn(const Session& session, KiottySpecItemKind kind,
                                         uint32_t relation);
std::string describeColumn(const Session& session, KiottySpecItemHandle handle,
                           const Column& column, const std::string& id);
std::optional<std::vector<TagColumn>> chooseDiffTags(const Session& session,
                                                     KiottySpecItemKind kind);
std::optional<std::vector<RelationColumn>> chooseDiffRelations(const Session& session,
                                                               KiottySpecItemKind kind);

}

std::optional<ColumnPlan> planColumns(const Session& session, KiottySpecItemKind kind)
{
    const std::optional<ColumnPlan> tag_plan = planTagColumns(session, kind);
    if (!tag_plan) { return std::nullopt; }
    ColumnPlan plan = *tag_plan;
    const std::optional<std::vector<uint32_t>> relations = chooseRelations(session, kind);
    if (!relations) { return std::nullopt; }
    for (const uint32_t relation : *relations) {
        const std::optional<Column> column = readRelationColumn(session, kind, relation);
        if (!column) { return std::nullopt; }
        plan.columns.push_back(*column);
    }
    return plan;
}

std::optional<DiffColumns> planDiffColumns(const Session& session, KiottySpecItemKind kind)
{
    const std::optional<std::vector<TagColumn>> tags = chooseDiffTags(session, kind);
    if (!tags) { return std::nullopt; }
    const std::optional<std::vector<RelationColumn>> relations =
        chooseDiffRelations(session, kind);
    if (!relations) { return std::nullopt; }
    return DiffColumns{*tags, *relations};
}

int printItemCursor(const Session& session, const KiottySpecCursorResult& cursor,
                    const ColumnPlan* plan)
{
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printItemHeader(plan);
    const std::unique_ptr<KiottySpecItemPage> page_storage = std::make_unique<KiottySpecItemPage>();
    KiottySpecItemPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchItemPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, fetched.code); }
        if (page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            printItem(session, page.elements[index], plan);
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

void printItemHeader(const ColumnPlan* plan)
{
    std::vector<std::string> header = {"id", "kind", "title", "summary", "checkpoint", "parent"};
    if (plan != nullptr) {
        for (const Column& column : plan->columns) { header.push_back(column.header); }
    }
    printRow(header);
}

void printItem(const Session& session, const KiottySpecItem& item, const ColumnPlan* plan)
{
    std::vector<std::string> row = { item.id,
                                     kindName(item.kind),
                                     cell(item.title),
                                     cell(item.summary),
                                     checkpointName(item.checkpoint),
                                     itemId(session, item.parent) };
    if (plan != nullptr) {
        const KiottySpecItemHandleResult handle =
            KiottySpec_findItem(session.context, textOf(item.id));
        for (const Column& column : plan->columns) {
            row.push_back(describeColumn(session, handle.value, column, item.id));
        }
    }
    printRow(row);
}

namespace {

std::optional<ColumnPlan> planTagColumns(const Session& session, KiottySpecItemKind kind)
{
    ColumnPlan plan {};
    for (const std::string& spec : session.args.all("tag")) {
        const std::optional<TagSpec> parsed = parseTagSpec(session, kind, spec);
        if (!parsed) { return std::nullopt; }
        const TagSpec tag_spec = *parsed;
        const bool has_mask = tag_spec.mask.has_value();
        const bool is_full = has_mask && plan.queries.size() == KIOTTY_SPEC_MAX_TAG_QUERIES;
        if (is_full) {
            printTagQueryLimit();
            return std::nullopt;
        }
        if (has_mask) {
            plan.queries.push_back(KiottySpecTagQuery{*tag_spec.mask, kind, tag_spec.tag.index});
        }
        const bool is_listed = hasTagColumn(plan.columns, tag_spec.tag.index);
        if (!is_listed) { plan.columns.push_back(makeTagColumn(tag_spec.tag)); }
    }
    const bool has_tags_flag = session.args.flag("tags");
    const std::vector<TagColumn> all_tags =
        has_tags_flag ? readAllTags(session, kind) : std::vector<TagColumn>();
    for (const TagColumn& tag : all_tags) {
        const bool is_listed = hasTagColumn(plan.columns, tag.index);
        if (!is_listed) { plan.columns.push_back(makeTagColumn(tag)); }
    }
    return plan;
}

std::optional<TagSpec> parseTagSpec(const Session& session, KiottySpecItemKind kind,
                                    const std::string& spec)
{
    const size_t equal = spec.find('=');
    const std::optional<TagColumn> tag = findTag(session, kind, spec.substr(0, equal));
    if (!tag) { return std::nullopt; }
    const bool has_value = equal != std::string::npos;
    if (!has_value) { return TagSpec{*tag, std::nullopt}; }
    const std::optional<uint64_t> mask = parseBits(tag->definition, spec.substr(equal + 1));
    if (!mask) { return std::nullopt; }
    return TagSpec{*tag, mask};
}

std::vector<TagColumn> readAllTags(const Session& session, KiottySpecItemKind kind)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::vector<TagColumn> tags {};
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        TagColumn tag {index, {}};
        const KiottySpecStatusResult read =
            KiottySpec_readTagDefinition(session.context, kind, index, &tag.definition);
        if (read.code == KIOTTY_SPEC_STATUS_SUCCESS) { tags.push_back(tag); }
    }
    return tags;
}

bool hasTagColumn(const std::vector<Column>& columns, uint32_t index)
{
    for (const Column& column : columns) {
        if (column.is_tag && column.tag.index == index) { return true; }
    }
    return false;
}

Column makeTagColumn(const TagColumn& tag)
{
    Column column {};
    column.header = tag.definition.name;
    column.is_tag = true;
    column.tag = tag;
    return column;
}

void printTagQueryLimit()
{
    const std::string limit = std::to_string(KIOTTY_SPEC_MAX_TAG_QUERIES);
    std::cerr << "error: " << formatMessage(MessageKey::TAG_QUERY_LIMIT, {limit}) << "\n";
}

std::optional<std::vector<uint32_t>> chooseRelations(const Session& session,
                                                     KiottySpecItemKind kind)
{
    std::vector<uint32_t> relations {};
    for (const std::string& name : session.args.all("relation")) {
        const std::optional<uint32_t> relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(*relation);
    }
    const bool has_relations_flag = session.args.flag("relations");
    if (has_relations_flag) {
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
        for (uint32_t index = 0; index < model.value.relation_count; ++index) {
            relations.push_back(index);
        }
    }
    return relations;
}

std::optional<Column> readRelationColumn(const Session& session, KiottySpecItemKind kind,
                                         uint32_t relation)
{
    Column column {};
    column.header = KiottySpec_readRelationDefinition(session.context, kind, relation).value.name;
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationCursorByModel(session.context, kind, relation);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, cursor.code);
        return std::nullopt;
    }
    const std::unique_ptr<KiottySpecRelationPage> page_storage =
        std::make_unique<KiottySpecRelationPage>();
    KiottySpecRelationPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRelation& element = page.elements[index];
            const std::string from_id = itemId(session, element.from);
            column.targets.try_emplace(from_id);
            std::string& targets = column.targets.at(from_id);
            targets += (targets.empty() ? "" : ",") + itemId(session, element.to);
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return column;
}

std::string describeColumn(const Session& session, KiottySpecItemHandle handle,
                           const Column& column, const std::string& id)
{
    if (column.is_tag) {
        const KiottySpecTagResult tag =
            KiottySpec_readItemTag(session.context, handle, column.tag.index);
        const uint64_t value = tag.code == KIOTTY_SPEC_STATUS_SUCCESS ? tag.value.value : 0;
        return bitNames(column.tag.definition, value);
    }
    const std::map<std::string, std::string>::const_iterator found = column.targets.find(id);
    return found == column.targets.end() ? "-" : found->second;
}

std::optional<std::vector<TagColumn>> chooseDiffTags(const Session& session,
                                                     KiottySpecItemKind kind)
{
    std::vector<TagColumn> tags {};
    for (const std::string& name : session.args.all("tag")) {
        const std::optional<TagColumn> tag = findTag(session, kind, name);
        if (!tag) { return std::nullopt; }
        tags.push_back(*tag);
    }
    const bool has_tags_flag = session.args.flag("tags");
    if (!has_tags_flag) { return tags; }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::vector<TagColumn> all_tags {};
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        TagColumn tag {index, {}};
        KiottySpec_readTagDefinition(session.context, kind, index, &tag.definition);
        all_tags.push_back(tag);
    }
    return all_tags;
}

std::optional<std::vector<RelationColumn>> chooseDiffRelations(const Session& session,
                                                               KiottySpecItemKind kind)
{
    std::vector<RelationColumn> relations {};
    for (const std::string& name : session.args.all("relation")) {
        const std::optional<uint32_t> relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(RelationColumn{*relation, name});
    }
    const bool has_relations_flag = session.args.flag("relations");
    if (!has_relations_flag) { return relations; }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::vector<RelationColumn> all_relations {};
    for (uint32_t index = 0; index < model.value.relation_count; ++index) {
        const KiottySpecRelationDefinitionResult definition =
            KiottySpec_readRelationDefinition(session.context, kind, index);
        all_relations.push_back(RelationColumn{index, definition.value.name});
    }
    return all_relations;
}

}

}
