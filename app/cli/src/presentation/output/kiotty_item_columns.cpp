#include "presentation/commands/kiotty_commands.h"

#include <cstdio>
#include <iostream>
#include <memory>

namespace kiotty::cli {

namespace {

struct TagSpec {
    TagColumn tag {};
    std::optional<uint64_t> mask {};
};

using RelationTargets = std::map<std::string, std::string>;

std::optional<ColumnPlan> planTagColumns(const Session& session, KiottySpecItemKind kind);
std::optional<TagSpec> resolveTagSpec(const Session& session, KiottySpecItemKind kind,
                                      const std::string& spec);
bool hasTagColumn(const std::vector<Column>& columns, uint32_t index);
Column makeTagColumn(const TagColumn& tag);
void printTagQueryLimit();
std::optional<std::vector<RelationColumn>> chooseRelations(const Session& session,
                                                           KiottySpecItemKind kind);
std::optional<Column> readRelationColumn(const Session& session, KiottySpecItemKind kind,
                                         const RelationColumn& relation);
RelationTargets collectRelationTargets(const Session& session, KiottySpecCursor cursor);
Column makeRelationColumn(const std::string& header, const RelationTargets& targets);
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
    const std::optional<std::vector<RelationColumn>> relations = chooseRelations(session, kind);
    if (!relations) { return std::nullopt; }
    for (const RelationColumn& relation : *relations) {
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
                    const ColumnPlan& plan)
{
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printItemHeader(plan);
    const std::unique_ptr<KiottySpecItemPage> page_storage = std::make_unique<KiottySpecItemPage>();
    KiottySpecItemPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchItemPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, fetched.code); }
        if (page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            printItem(session, page.elements[index], plan);
        }
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    return 0;
}

void printItemHeader(const ColumnPlan& plan)
{
    std::vector<std::string> header = {"id", "kind", "title", "summary", "checkpoint", "parent"};
    for (const Column& column : plan.columns) { header.push_back(column.header); }
    printRow(header);
}

void printItem(const Session& session, const KiottySpecItem& item, const ColumnPlan& plan)
{
    std::vector<std::string> row = { item.id,
                                     kindName(item.kind),
                                     cell(item.title),
                                     cell(item.summary),
                                     checkpointName(item.checkpoint),
                                     itemId(session, item.parent) };
    const bool has_columns = !plan.columns.empty();
    if (has_columns) {
        const KiottySpecItemHandleResult handle =
            KiottySpec_findItem(&session.context, textOf(item.id));
        for (const Column& column : plan.columns) {
            row.push_back(describeColumn(session, handle.value, column, item.id));
        }
    }
    printRow(row);
}

namespace {

std::optional<ColumnPlan> planTagColumns(const Session& session, KiottySpecItemKind kind)
{
    ColumnPlan plan {};
    for (const std::string& spec : session.arguments.all("tag")) {
        const std::optional<TagSpec> parsed = resolveTagSpec(session, kind, spec);
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
    const bool has_tags_flag = session.arguments.flag("tags");
    const std::vector<TagColumn> all_tags =
        has_tags_flag ? readTagDefinitions(session, kind) : std::vector<TagColumn>();
    for (const TagColumn& tag : all_tags) {
        const bool is_listed = hasTagColumn(plan.columns, tag.index);
        if (!is_listed) { plan.columns.push_back(makeTagColumn(tag)); }
    }
    return plan;
}

std::optional<TagSpec> resolveTagSpec(const Session& session, KiottySpecItemKind kind,
                                      const std::string& spec)
{
    const std::optional<TagSpecText> parsed = parseTagSpec(spec);
    if (!parsed) {
        usage(findMessage(MessageKey::TAG_OPTION_USAGE));
        return std::nullopt;
    }
    const std::optional<TagColumn> tag = findTag(session, kind, parsed.value().name);
    if (!tag) { return std::nullopt; }
    const std::optional<std::string> bits = parsed.value().bits;
    if (!bits) { return TagSpec{*tag, std::nullopt}; }
    const std::optional<uint64_t> mask = parseBits(tag.value().definition, *bits);
    if (!mask) { return std::nullopt; }
    return TagSpec{*tag, mask};
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

std::optional<std::vector<RelationColumn>> chooseRelations(const Session& session,
                                                           KiottySpecItemKind kind)
{
    std::vector<RelationColumn> relations {};
    for (const std::string& name : session.arguments.all("relation")) {
        const std::optional<uint32_t> relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(RelationColumn{*relation, name});
    }
    const bool has_relations_flag = session.arguments.flag("relations");
    const std::vector<KiottySpecRelationDefinition> definitions =
        has_relations_flag ? readRelationDefinitions(session, kind)
                           : std::vector<KiottySpecRelationDefinition>();
    for (const KiottySpecRelationDefinition& definition : definitions) {
        relations.push_back(RelationColumn{definition.index, definition.name});
    }
    return relations;
}

std::optional<Column> readRelationColumn(const Session& session, KiottySpecItemKind kind,
                                         const RelationColumn& relation)
{
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationCursorByModel(&session.context, kind, relation.index);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, cursor.code);
        return std::nullopt;
    }
    const RelationTargets targets = collectRelationTargets(session, cursor.value);
    KiottySpec_closeCursor(&session.context, cursor.value);
    return makeRelationColumn(relation.name, targets);
}

RelationTargets collectRelationTargets(const Session& session, KiottySpecCursor cursor)
{
    RelationTargets targets {};
    const std::unique_ptr<KiottySpecRelationPage> page_storage =
        std::make_unique<KiottySpecRelationPage>();
    KiottySpecRelationPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationPage(&session.context, cursor, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRelation& relation = page.elements[index];
            const std::string from_id = itemId(session, relation.from);
            targets.try_emplace(from_id);
            std::string& joined = targets.at(from_id);
            joined += (joined.empty() ? "" : ",") + itemId(session, relation.to);
        }
    }
    return targets;
}

Column makeRelationColumn(const std::string& header, const RelationTargets& targets)
{
    Column column {};
    column.header = header;
    column.targets = targets;
    return column;
}

std::string describeColumn(const Session& session, KiottySpecItemHandle handle,
                           const Column& column, const std::string& id)
{
    if (column.is_tag) {
        const KiottySpecTagResult tag =
            KiottySpec_readItemTag(&session.context, handle, column.tag.index);
        const uint64_t value = tag.code == KIOTTY_SPEC_STATUS_SUCCESS ? tag.value.value : 0;
        return bitNames(column.tag.definition, value);
    }
    const bool has_targets = column.targets.count(id) > 0;
    return has_targets ? column.targets.at(id) : "-";
}

std::optional<std::vector<TagColumn>> chooseDiffTags(const Session& session,
                                                     KiottySpecItemKind kind)
{
    std::vector<TagColumn> tags {};
    for (const std::string& name : session.arguments.all("tag")) {
        const std::optional<TagColumn> tag = findTag(session, kind, name);
        if (!tag) { return std::nullopt; }
        tags.push_back(*tag);
    }
    const bool has_tags_flag = session.arguments.flag("tags");
    if (!has_tags_flag) { return tags; }
    return readTagDefinitions(session, kind);
}

std::optional<std::vector<RelationColumn>> chooseDiffRelations(const Session& session,
                                                               KiottySpecItemKind kind)
{
    std::vector<RelationColumn> relations {};
    for (const std::string& name : session.arguments.all("relation")) {
        const std::optional<uint32_t> relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(RelationColumn{*relation, name});
    }
    const bool has_relations_flag = session.arguments.flag("relations");
    if (!has_relations_flag) { return relations; }
    const std::vector<KiottySpecRelationDefinition> definitions =
        readRelationDefinitions(session, kind);
    std::vector<RelationColumn> all_relations {};
    for (const KiottySpecRelationDefinition& definition : definitions) {
        all_relations.push_back(RelationColumn{definition.index, definition.name});
    }
    return all_relations;
}

}

std::optional<TagSpecText> parseTagSpec(std::string_view spec)
{
    const size_t equal = spec.find('=');
    const std::string_view name = spec.substr(0, equal);
    const bool has_null = name.find('\0') != std::string_view::npos;
    const bool is_name_valid =
        !name.empty() && name.size() <= KIOTTY_SPEC_MAX_TAG_NAME_LENGTH && !has_null;
    if (!is_name_valid) { return std::nullopt; }
    const bool has_bits = equal != std::string_view::npos;
    if (!has_bits) { return TagSpecText{std::string(name), std::nullopt}; }
    return TagSpecText{std::string(name), std::string(spec.substr(equal + 1))};
}

}
