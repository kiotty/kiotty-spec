#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <memory>

namespace kiotty::cli {

namespace {

constexpr uint32_t DEFAULT_IMPACT_DEPTH = 1;
constexpr size_t TAG_VALUE_POSITION = 2;
constexpr size_t RELATION_TARGET_POSITION = 2;

enum class RelationEdit { ADD, REMOVE };

KiottySpecIdResult addDraftItem(const Session& session, KiottySpecItemKind kind,
                                const std::string& title);
int setTags(const Session& session, const std::string& id, KiottySpecItemKind kind);
int applyRelationChange(const Session& session, RelationEdit edit);

}

int initProject(const Session& session)
{
    const std::string name = session.args.option("name").value_or("");
    return check(session, KiottySpec_initializeProject(session.context, textOf(name)).code);
}

int addItem(const Session& session)
{
    const std::string* kind_name = positional(session, 0);
    const std::optional<std::string> title = session.args.option("title");
    const bool has_arguments = kind_name != nullptr && title.has_value();
    if (!has_arguments) { return usage(findMessage(MessageKey::ITEM_ADD_USAGE)); }
    const std::optional<KiottySpecItemKind> kind = kindArgument(session, *kind_name);
    if (!kind) { return USAGE_EXIT_CODE; }
    const BodyOption body = bodyOption(session, "body", "body-file");
    if (body.is_unreadable) { return USAGE_EXIT_CODE; }
    const KiottySpecIdResult created = addDraftItem(session, *kind, *title);
    if (created.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, created.code); }
    const std::string id = created.value;
    const std::optional<std::string> parent = session.args.option("parent");
    if (parent) {
        const KiottySpecStatusResult set =
            KiottySpec_setItemParent(session.context, textOf(id), textOf(*parent));
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    if (body.text) {
        const KiottySpecStatusResult written =
            KiottySpec_writeItemBody(session.context, textOf(id), textOf(*body.text));
        if (written.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written.code); }
    }
    const int tagged = setTags(session, id, *kind);
    if (tagged != 0) { return tagged; }
    std::printf("%s\n", id.c_str());
    return 0;
}

int setItem(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) {
        return usage(findMessage(MessageKey::ITEM_SET_USAGE));
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const std::optional<std::string> title = session.args.option("title");
    const std::optional<std::string> summary = session.args.option("summary");
    if (title || summary) {
        const KiottySpecText title_text = title ? textOf(*title) : KiottySpecText{nullptr, 0};
        const KiottySpecText summary_text = summary ? textOf(*summary) : KiottySpecText{nullptr, 0};
        const KiottySpecStatusResult updated =
            KiottySpec_updateItem(session.context, textOf(*id), title_text, summary_text);
        if (updated.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, updated.code); }
    }
    const std::optional<std::string> parent = session.args.option("parent");
    if (parent) {
        const std::string target = *parent == "-" ? std::string() : *parent;
        const KiottySpecStatusResult set =
            KiottySpec_setItemParent(session.context, textOf(*id), textOf(target));
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    const BodyOption body = bodyOption(session, "body", "body-file");
    if (body.is_unreadable) { return USAGE_EXIT_CODE; }
    if (body.text) {
        const KiottySpecStatusResult written =
            KiottySpec_writeItemBody(session.context, textOf(*id), textOf(*body.text));
        if (written.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written.code); }
    }
    return setTags(session, *id, item.value.kind);
}

int showItem(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) {
        return usage(findMessage(MessageKey::ITEM_SHOW_USAGE));
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const std::optional<ColumnPlan> plan = planColumns(session, item.value.kind);
    if (!plan) { return USAGE_EXIT_CODE; }
    printItemHeader(&*plan);
    printItem(session, item.value, &*plan);
    return 0;
}

int renameItem(const Session& session)
{
    const std::string* id = positional(session, 0);
    const std::string* next = positional(session, 1);
    if (id == nullptr || next == nullptr) {
        return usage(findMessage(MessageKey::ITEM_RENAME_USAGE));
    }
    return check(session, KiottySpec_renameItem(session.context, textOf(*id), textOf(*next)).code);
}

int listItems(const Session& session)
{
    KiottySpecItemKind kind = static_cast<KiottySpecItemKind>(0);
    const std::optional<std::string> name = session.args.option("kind");
    if (name) {
        const std::optional<KiottySpecItemKind> parsed = kindArgument(session, *name);
        if (!parsed) { return USAGE_EXIT_CODE; }
        kind = *parsed;
    }
    const bool has_column_options =
        !session.args.all("tag").empty() || !session.args.all("relation").empty() ||
        session.args.flag("tags") || session.args.flag("relations");
    if (kind == 0 && has_column_options) {
        return usage(findMessage(MessageKey::ITEM_LIST_COLUMNS_NEED_KIND));
    }
    std::optional<ColumnPlan> plan {};
    if (kind != 0) {
        plan = planColumns(session, kind);
        if (!plan) { return USAGE_EXIT_CODE; }
    }
    const KiottySpecTagQuery* queries =
        plan && !plan->queries.empty() ? plan->queries.data() : nullptr;
    const size_t count = plan ? plan->queries.size() : 0;
    return printItemCursor(session,
                           KiottySpec_openItemCursor(session.context, kind, queries, count),
                           plan ? &*plan : nullptr);
}

int listChildren(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::ITEM_CHILDREN_USAGE)); }
    return printItemCursor(session, KiottySpec_openChildCursor(session.context, textOf(*id)),
                           nullptr);
}

int showBody(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::ITEM_BODY_USAGE)); }
    std::vector<char> buffer(KIOTTY_SPEC_MAX_BODY_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readItemBody(
        session.context, textOf(*id), KiottySpecBuffer { buffer.data(), buffer.size() });
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, read.code); }
    std::fwrite(buffer.data(), 1, read.value, stdout);
    if (read.value > 0 && buffer[read.value - 1] != '\n') { std::fputc('\n', stdout); }
    return 0;
}

int removeItem(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::ITEM_REMOVE_USAGE)); }
    return check(session, KiottySpec_removeItem(session.context, textOf(*id)).code);
}

int showTags(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::TAG_SHOW_USAGE)); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, item.value.kind);
    printRow({"tag", "value"});
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        const std::unique_ptr<KiottySpecTagDefinition> definition_storage =
            std::make_unique<KiottySpecTagDefinition>();
        KiottySpecTagDefinition& definition = *definition_storage;
        const KiottySpecStatusResult read =
            KiottySpec_readTagDefinition(session.context, item.value.kind, index, &definition);
        if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        const KiottySpecTagResult tag = KiottySpec_readItemTag(session.context, item.handle, index);
        printRow(
            { definition.name,
              bitNames(definition, tag.code == KIOTTY_SPEC_STATUS_SUCCESS ? tag.value.value : 0) });
    }
    return 0;
}

int setTag(const Session& session)
{
    const std::string* id = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* bits = positional(session, TAG_VALUE_POSITION);
    if (id == nullptr || name == nullptr || bits == nullptr) {
        return usage(findMessage(MessageKey::TAG_SET_USAGE));
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const std::optional<TagColumn> tag = findTag(session, item.value.kind, *name);
    if (!tag) { return USAGE_EXIT_CODE; }
    const std::optional<uint64_t> value = parseBits(tag->definition, *bits);
    if (!value) { return USAGE_EXIT_CODE; }
    return check(session,
                 KiottySpec_setItemTag(session.context, item.handle, tag->index, *value).code);
}

int addRelation(const Session& session)
{ return applyRelationChange(session, RelationEdit::ADD); }

int removeRelation(const Session& session)
{ return applyRelationChange(session, RelationEdit::REMOVE); }

int listRelations(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::RELATION_LIST_USAGE)); }
    KiottySpecDirection direction = KIOTTY_SPEC_DIRECTION_BOTH;
    const bool has_up_flag = session.args.flag("up");
    const bool has_down_flag = session.args.flag("down");
    if (has_up_flag) { direction = KIOTTY_SPEC_DIRECTION_UPSTREAM; }
    if (has_down_flag) { direction = KIOTTY_SPEC_DIRECTION_DOWNSTREAM; }
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationCursor(session.context, textOf(*id), direction);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"from", "relation", "to"});
    const std::unique_ptr<KiottySpecRelationPage> page_storage =
        std::make_unique<KiottySpecRelationPage>();
    KiottySpecRelationPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRelation& relation = page.elements[index];
            const KiottySpecRelationDefinitionResult definition = KiottySpec_readRelationDefinition(
                session.context, relation.model, relation.relation);
            printRow({ itemId(session, relation.from), definition.value.name,
                       itemId(session, relation.to) });
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int showImpact(const Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage(findMessage(MessageKey::IMPACT_USAGE)); }
    uint32_t depth = DEFAULT_IMPACT_DEPTH;
    const std::optional<std::string> text = session.args.option("depth");
    if (text) {
        depth = static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, DECIMAL_BASE));
    }
    const KiottySpecCursorResult cursor =
        KiottySpec_openImpactCursor(session.context, textOf(*id), depth);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"id"});
    const std::unique_ptr<KiottySpecIdPage> page_storage = std::make_unique<KiottySpecIdPage>();
    KiottySpecIdPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchIdPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) { printRow({page.elements[index]}); }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

namespace {

KiottySpecIdResult addDraftItem(const Session& session, KiottySpecItemKind kind,
                                const std::string& title)
{
    const std::string summary = session.args.option("summary").value_or("");
    const std::string given = session.args.option("id").value_or("");
    KiottySpecItemDraft draft {};
    draft.id = textOf(given);
    draft.title = textOf(title);
    draft.summary = textOf(summary);
    draft.kind = kind;
    return KiottySpec_createItem(session.context, &draft);
}

int setTags(const Session& session, const std::string& id, KiottySpecItemKind kind)
{
    for (const std::string& spec : session.args.all("tag")) {
        const size_t equal = spec.find('=');
        if (equal == std::string::npos) { return usage(findMessage(MessageKey::TAG_OPTION_USAGE)); }
        const std::optional<TagColumn> tag = findTag(session, kind, spec.substr(0, equal));
        if (!tag) { return USAGE_EXIT_CODE; }
        const std::optional<uint64_t> value = parseBits(tag->definition, spec.substr(equal + 1));
        if (!value) { return USAGE_EXIT_CODE; }
        const KiottySpecItemHandleResult handle = KiottySpec_findItem(session.context, textOf(id));
        if (handle.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, handle.code); }
        const KiottySpecStatusResult set =
            KiottySpec_setItemTag(session.context, handle.value, tag->index, *value);
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    return 0;
}

int applyRelationChange(const Session& session, RelationEdit edit)
{
    const std::string* from = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* to = positional(session, RELATION_TARGET_POSITION);
    if (from == nullptr || name == nullptr || to == nullptr) {
        return usage(findMessage(MessageKey::RELATION_CHANGE_USAGE));
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*from));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const std::optional<uint32_t> relation = findRelation(session, item.value.kind, *name);
    if (!relation) { return USAGE_EXIT_CODE; }
    const KiottySpecStatusResult changed =
        edit == RelationEdit::ADD
            ? KiottySpec_addRelation(session.context, textOf(*from), *relation, textOf(*to))
            : KiottySpec_removeRelation(session.context, textOf(*from), *relation, textOf(*to));
    return check(session, changed.code);
}

}

}
