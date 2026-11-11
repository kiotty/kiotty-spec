#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>
#include <map>

namespace kiotty::cli {

namespace {

constexpr uint32_t DEFAULT_IMPACT_DEPTH = 1;

int setTags(Session& session, const std::string& id, KiottySpecItemKind kind)
{
    for (const std::string& spec : session.args.all("tag")) {
        const size_t equal = spec.find('=');
        if (equal == std::string::npos) { return usage("--tag 이름=값[,값]"); }
        const auto tag = findTag(session, kind, spec.substr(0, equal));
        if (!tag) { return 2; }
        const auto value = parseBits(tag->definition, spec.substr(equal + 1));
        if (!value) { return 2; }
        const KiottySpecItemHandleResult handle = KiottySpec_findItem(session.context, textOf(id));
        if (handle.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, handle.code); }
        const KiottySpecStatusResult set =
            KiottySpec_setItemTag(session.context, handle.value, tag->index, *value);
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    return 0;
}

int changeRelation(Session& session, bool add)
{
    const std::string* from = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* to = positional(session, 2);
    if (from == nullptr || name == nullptr || to == nullptr) {
        return usage("ks relation add|remove FROM 관계이름 TO");
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*from));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto relation = findRelation(session, item.value.kind, *name);
    if (!relation) { return 2; }
    const KiottySpecStatusResult changed =
        add ? KiottySpec_addRelation(session.context, textOf(*from), *relation, textOf(*to))
            : KiottySpec_removeRelation(session.context, textOf(*from), *relation, textOf(*to));
    return check(session, changed.code);
}

}

int initProject(Session& session)
{
    const std::string name = session.args.option("name").value_or("");
    return check(session, KiottySpec_initializeProject(session.context, textOf(name)).code);
}

int addItem(Session& session)
{
    const std::string* kind_name = positional(session, 0);
    const auto title = session.args.option("title");
    if (kind_name == nullptr || !title) { return usage("ks item add KIND --title T"); }
    const auto kind = kindArgument(session, *kind_name);
    if (!kind) { return 2; }
    const BodyOption body = bodyOption(session, "body", "body-file");
    if (body.is_unreadable) { return 2; }
    const std::string summary = session.args.option("summary").value_or("");
    const std::string given = session.args.option("id").value_or("");
    KiottySpecItemDraft draft {};
    draft.id = textOf(given);
    draft.title = textOf(*title);
    draft.summary = textOf(summary);
    draft.kind = *kind;
    const KiottySpecIdResult created = KiottySpec_createItem(session.context, &draft);
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

int setItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) {
        return usage("ks item set ID [--title T] [--summary S] [--parent ID|-] [--body "
                     "T|--body-file F] [--tag 이름=값]");
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto title = session.args.option("title");
    const auto summary = session.args.option("summary");
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
    if (body.is_unreadable) { return 2; }
    if (body.text) {
        const KiottySpecStatusResult written =
            KiottySpec_writeItemBody(session.context, textOf(*id), textOf(*body.text));
        if (written.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written.code); }
    }
    return setTags(session, *id, item.value.kind);
}

int showItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) {
        return usage("ks item show ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]");
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto plan = planColumns(session, item.value.kind);
    if (!plan) { return 2; }
    printItemHeader(&*plan);
    printItem(session, item.value, &*plan);
    return 0;
}

int renameItem(Session& session)
{
    const std::string* id = positional(session, 0);
    const std::string* next = positional(session, 1);
    if (id == nullptr || next == nullptr) { return usage("ks item rename ID 새ID"); }
    return check(session, KiottySpec_renameItem(session.context, textOf(*id), textOf(*next)).code);
}

int listItems(Session& session)
{
    KiottySpecItemKind kind = static_cast<KiottySpecItemKind>(0);
    const std::optional<std::string> name = session.args.option("kind");
    if (name) {
        const auto parsed = kindArgument(session, *name);
        if (!parsed) { return 2; }
        kind = *parsed;
    }
    const bool wants_columns = !session.args.all("tag").empty() ||
                               !session.args.all("relation").empty() || session.args.flag("tags") ||
                               session.args.flag("relations");
    if (kind == 0 && wants_columns) { return usage("--tag · --relation 은 --kind 와 함께 쓴다"); }
    std::optional<ColumnPlan> plan {};
    if (kind != 0) {
        plan = planColumns(session, kind);
        if (!plan) { return 2; }
    }
    const KiottySpecTagQuery* queries =
        plan && !plan->queries.empty() ? plan->queries.data() : nullptr;
    const size_t count = plan ? plan->queries.size() : 0;
    return printItemCursor(session,
                           KiottySpec_openItemCursor(session.context, kind, queries, count),
                           plan ? &*plan : nullptr);
}

int listChildren(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item children ID"); }
    return printItemCursor(session, KiottySpec_openChildCursor(session.context, textOf(*id)),
                           nullptr);
}

int showBody(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item body ID"); }
    std::vector<char> buffer(KIOTTY_SPEC_MAX_BODY_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readItemBody(
        session.context, textOf(*id), KiottySpecBuffer { buffer.data(), buffer.size() });
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, read.code); }
    std::fwrite(buffer.data(), 1, read.value, stdout);
    if (read.value > 0 && buffer[read.value - 1] != '\n') { std::fputc('\n', stdout); }
    return 0;
}

int removeItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item remove ID"); }
    return check(session, KiottySpec_removeItem(session.context, textOf(*id)).code);
}

int showTags(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks tag show ID"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, item.value.kind);
    printRow({"tag", "value"});
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        KiottySpecTagDefinition definition {};
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

int setTag(Session& session)
{
    const std::string* id = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* bits = positional(session, 2);
    if (id == nullptr || name == nullptr || bits == nullptr) {
        return usage("ks tag set ID 태그이름 값[,값]|-");
    }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto tag = findTag(session, item.value.kind, *name);
    if (!tag) { return 2; }
    const auto value = parseBits(tag->definition, *bits);
    if (!value) { return 2; }
    return check(session,
                 KiottySpec_setItemTag(session.context, item.handle, tag->index, *value).code);
}

int addRelation(Session& session) { return changeRelation(session, true); }
int removeRelation(Session& session) { return changeRelation(session, false); }

int listRelations(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks relation list ID [--up|--down]"); }
    KiottySpecDirection direction = KIOTTY_SPEC_DIRECTION_BOTH;
    const bool has_up_flag = session.args.flag("up");
    const bool has_down_flag = session.args.flag("down");
    if (has_up_flag) { direction = KIOTTY_SPEC_DIRECTION_UPSTREAM; }
    if (has_down_flag) { direction = KIOTTY_SPEC_DIRECTION_DOWNSTREAM; }
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationCursor(session.context, textOf(*id), direction);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"from", "relation", "to"});
    KiottySpecRelationPage page {};
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

int showImpact(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks impact ID [--depth N]"); }
    uint32_t depth = DEFAULT_IMPACT_DEPTH;
    const std::optional<std::string> text = session.args.option("depth");
    if (text) {
        depth = static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, 10));
    }
    const KiottySpecCursorResult cursor =
        KiottySpec_openImpactCursor(session.context, textOf(*id), depth);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"id"});
    KiottySpecIdPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchIdPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) { printRow({page.elements[index]}); }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

}
