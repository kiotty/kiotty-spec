#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>

namespace kiotty::cli {

std::optional<ColumnPlan> planColumns(Session& session, KiottySpecItemKind kind)
{
    ColumnPlan plan {};
    auto addTag = [&](const TagColumn& tag) {
        for (const Column& column : plan.columns) {
            if (column.is_tag && column.tag.index == tag.index) { return; }
        }
        Column column {};
        column.header = tag.definition.name;
        column.is_tag = true;
        column.tag = tag;
        plan.columns.push_back(column);
    };
    for (const std::string& spec : session.args.all("tag")) {
        const size_t equal = spec.find('=');
        const auto tag = findTag(session, kind, spec.substr(0, equal));
        if (!tag) { return std::nullopt; }
        if (equal != std::string::npos) {
            const auto mask = parseBits(tag->definition, spec.substr(equal + 1));
            if (!mask) { return std::nullopt; }
            const bool is_full = plan.queries.size() == KIOTTY_SPEC_MAX_TAG_QUERIES;
            if (is_full) {
                std::cerr << "error: 태그 조건은 " << KIOTTY_SPEC_MAX_TAG_QUERIES << " 개까지\n";
                return std::nullopt;
            }
            plan.queries.push_back(KiottySpecTagQuery{*mask, kind, tag->index});
        }
        addTag(*tag);
    }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    const bool has_tags_flag = session.args.flag("tags");
    if (has_tags_flag) {
        for (uint32_t index = 0; index < model.value.tag_count; ++index) {
            TagColumn tag {index, {}};
            const KiottySpecStatusResult read =
                KiottySpec_readTagDefinition(session.context, kind, index, &tag.definition);
            if (read.code == KIOTTY_SPEC_STATUS_SUCCESS) { addTag(tag); }
        }
    }
    std::vector<uint32_t> relations {};
    for (const std::string& name : session.args.all("relation")) {
        const auto relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(*relation);
    }
    const bool has_relations_flag = session.args.flag("relations");
    if (has_relations_flag) {
        for (uint32_t index = 0; index < model.value.relation_count; ++index) {
            relations.push_back(index);
        }
    }
    for (const uint32_t relation : relations) {
        Column column {};
        column.header =
            KiottySpec_readRelationDefinition(session.context, kind, relation).value.name;
        const KiottySpecCursorResult cursor =
            KiottySpec_openRelationCursorByModel(session.context, kind, relation);
        if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
            fail(session, cursor.code);
            return std::nullopt;
        }
        KiottySpecRelationPage page {};
        while (true) {
            const KiottySpecStatusResult fetched =
                KiottySpec_fetchRelationPage(session.context, cursor.value, &page);
            if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
            for (size_t index = 0; index < page.length; ++index) {
                std::string& targets = column.targets[itemId(session, page.elements[index].from)];
                const bool has_targets = !targets.empty();
                if (has_targets) { targets += ','; }
                targets += itemId(session, page.elements[index].to);
            }
        }
        KiottySpec_closeCursor(session.context, cursor.value);
        plan.columns.push_back(column);
    }
    return plan;
}

void printItemHeader(const ColumnPlan* plan)
{
    std::vector<std::string> header = {"id", "kind", "title", "summary", "checkpoint", "parent"};
    if (plan != nullptr) {
        for (const Column& column : plan->columns) { header.push_back(column.header); }
    }
    printRow(header);
}

void printItem(Session& session, const KiottySpecItem& item, const ColumnPlan* plan)
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
            if (column.is_tag) {
                const KiottySpecTagResult tag =
                    KiottySpec_readItemTag(session.context, handle.value, column.tag.index);
                row.push_back(bitNames(column.tag.definition, tag.code == KIOTTY_SPEC_STATUS_SUCCESS
                                                                  ? tag.value.value
                                                                  : 0));
                continue;
            }
            const auto found = column.targets.find(item.id);
            row.push_back(found == column.targets.end() ? "-" : found->second);
        }
    }
    printRow(row);
}

int printItemCursor(Session& session, const KiottySpecCursorResult& cursor, const ColumnPlan* plan)
{
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printItemHeader(plan);
    KiottySpecItemPage page {};
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

}
