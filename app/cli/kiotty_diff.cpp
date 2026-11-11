
#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace kiotty::cli {

const std::vector<std::string_view> DIFF_FLAGS = { "added", "removed", "modified",
                                                   "title", "summary", "no-color" };

namespace {

constexpr const char* BLUE = "\x1b[94m";
constexpr const char* RED = "\x1b[31m";
constexpr const char* RESET = "\x1b[0m";
constexpr int DIFF_GROUP_COUNT = 3;

struct Painter;

bool detectColorTerminal();
std::string escape(std::string_view text);
std::string quote(std::string_view text);
std::optional<KiottySpecCheckpointHandle> parseCheckpointArgument(std::string_view text);
int printSummary(Session& session, const KiottySpecDiffQuery& query);
std::string diffSummary(Session& session, const KiottySpecItemDiff& diff,
                        const KiottySpecDiffSummaryResult& range, const Painter& painter,
                        const std::string& before, const std::string& after);

struct Painter {
    bool is_colored {};

    std::string paint(const KiottySpecTextDiff& diff) const
    {
        const std::string_view part(diff.text, diff.length);
        switch (diff.change) {
        case KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED: return escape(part);
        case KIOTTY_SPEC_TEXT_CHANGE_ADDED: return added(part);
        case KIOTTY_SPEC_TEXT_CHANGE_REMOVED: return removed(part);
        }
        return escape(part);
    }

    std::string added(std::string_view text) const
    {
        return is_colored ? std::string(BLUE) + escape(text) + RESET : "{+" + escape(text) + "+}";
    }

    std::string removed(std::string_view text) const
    {
        return is_colored ? std::string(RED) + escape(text) + RESET : "[-" + escape(text) + "-]";
    }

    std::string mark(bool add, const std::string& id) const
    {
        const std::string text = (add ? "+" : "-") + id;
        if (!is_colored) { return text; }
        return std::string(add ? BLUE : RED) + text + RESET;
    }
};

struct RelationChange {
    bool is_added {};
    std::string to_id {};
};

using RelationKey = std::pair<std::string, uint32_t>;

}

int runDiff(Session& session)
{
    const std::string* from_name = positional(session, 0);
    const std::string* to_name = positional(session, 1);
    const std::optional<KiottySpecCheckpointHandle> from =
        from_name == nullptr ? std::nullopt : parseCheckpointArgument(*from_name);
    const std::optional<KiottySpecCheckpointHandle> to =
        to_name == nullptr ? std::nullopt : parseCheckpointArgument(*to_name);
    const bool has_range = from.has_value() && to.has_value();
    if (!has_range) { return usage(findMessage(MessageKey::DIFF_USAGE)); }
    KiottySpecDiffQuery query {*from, *to, 0, 0};
    const auto kind_name = session.args.option("kind");
    if (!kind_name) { return printSummary(session, query); }

    const auto kind = kindArgument(session, *kind_name);
    if (!kind) { return USAGE_EXIT_CODE; }
    const bool has_added_flag = session.args.flag("added");
    const bool has_removed_flag = session.args.flag("removed");
    const bool has_modified_flag = session.args.flag("modified");
    const bool has_group_flag = has_added_flag || has_removed_flag || has_modified_flag;
    if (!has_group_flag) { return usage(findMessage(MessageKey::DIFF_GROUP_USAGE)); }
    const bool is_group_shown[DIFF_GROUP_COUNT] = { has_added_flag, has_removed_flag,
                                                    has_modified_flag };
    query.kind_mask = 1u << (*kind - 1);
    const KiottySpecDiffSummaryResult range = KiottySpec_summarizeDiff(session.context, &query);
    if (range.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, range.code); }
    const Painter painter {!session.args.flag("no-color") && detectColorTerminal()};

    std::vector<TagColumn> tags {};
    for (const std::string& name : session.args.all("tag")) {
        const auto tag = findTag(session, *kind, name);
        if (!tag) { return USAGE_EXIT_CODE; }
        tags.push_back(*tag);
    }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, *kind);
    const bool has_tags_flag = session.args.flag("tags");
    if (has_tags_flag) {
        tags.clear();
        for (uint32_t index = 0; index < model.value.tag_count; ++index) {
            TagColumn tag {index, {}};
            KiottySpec_readTagDefinition(session.context, *kind, index, &tag.definition);
            tags.push_back(tag);
        }
    }
    std::vector<std::pair<uint32_t, std::string>> relations {};
    for (const std::string& name : session.args.all("relation")) {
        const auto relation = findRelation(session, *kind, name);
        if (!relation) { return USAGE_EXIT_CODE; }
        relations.emplace_back(*relation, name);
    }
    const bool has_relations_flag = session.args.flag("relations");
    if (has_relations_flag) {
        relations.clear();
        for (uint32_t index = 0; index < model.value.relation_count; ++index) {
            relations.emplace_back(
                index, KiottySpec_readRelationDefinition(session.context, *kind, index).value.name);
        }
    }
    std::map<RelationKey, std::vector<RelationChange>> changes {};
    const bool has_relations = !relations.empty();
    if (has_relations) {
        const KiottySpecCursorResult cursor =
            KiottySpec_openRelationDiffCursor(session.context, &query);
        if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
        KiottySpecRelationDiffPage page {};
        while (true) {
            const KiottySpecStatusResult fetched =
                KiottySpec_fetchRelationDiffPage(session.context, cursor.value, &page);
            if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
            for (size_t index = 0; index < page.length; ++index) {
                const KiottySpecRelationDiff& diff = page.elements[index];
                const RelationKey key { diff.from_id, diff.relation };
                changes.try_emplace(key);
                changes.at(key).push_back(
                    RelationChange { diff.change == KIOTTY_SPEC_CHANGE_ADDED, diff.to_id });
            }
        }
        KiottySpec_closeCursor(session.context, cursor.value);
    }

    std::vector<KiottySpecItemDiff> diffs {};
    const KiottySpecCursorResult cursor = KiottySpec_openItemDiffCursor(session.context, &query);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    KiottySpecItemDiffPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchItemDiffPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        diffs.insert(diffs.end(), page.elements, page.elements + page.length);
    }
    KiottySpec_closeCursor(session.context, cursor.value);

    std::vector<std::string> header = {"id", "kind"};
    const bool has_title = session.args.flag("title");
    const bool has_summary = session.args.flag("summary");
    if (has_title) { header.push_back("title"); }
    if (has_summary) { header.push_back("summary"); }
    for (const TagColumn& tag : tags) { header.push_back(tag.definition.name); }
    for (const auto& relation : relations) { header.push_back(relation.second); }

    const KiottySpecChange order[DIFF_GROUP_COUNT] = { KIOTTY_SPEC_CHANGE_ADDED,
                                                       KIOTTY_SPEC_CHANGE_REMOVED,
                                                       KIOTTY_SPEC_CHANGE_MODIFIED };
    const char* labels[DIFF_GROUP_COUNT] = {"# added", "# removed", "# modified"};
    bool has_printed_group = false;
    for (int group = 0; group < DIFF_GROUP_COUNT; ++group) {
        const bool is_shown = is_group_shown[group];
        if (!is_shown) { continue; }
        if (has_printed_group) { std::fputc('\n', stdout); }
        has_printed_group = true;
        std::printf("%s\n", labels[group]);
        printRow(header);
        for (const KiottySpecItemDiff& diff : diffs) {
            if (diff.change != order[group]) { continue; }
            const bool is_added = diff.change == KIOTTY_SPEC_CHANGE_ADDED;
            const KiottySpecItemResult before =
                KiottySpec_readItemAt(session.context, textOf(diff.id), range.from);
            const KiottySpecItemResult after =
                KiottySpec_readItemAt(session.context, textOf(diff.id), range.to);
            const KiottySpecItem& shown =
                is_added || diff.change == KIOTTY_SPEC_CHANGE_MODIFIED ? after.value : before.value;
            const KiottySpecCheckpointHandle shown_at =
                is_added || diff.change == KIOTTY_SPEC_CHANGE_MODIFIED ? range.to : range.from;
            std::vector<std::string> row = {diff.id, kindName(diff.kind)};
            const bool is_modified = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED;
            if (has_title) {
                const bool is_title_changed =
                    is_modified && (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_TITLE) != 0;
                row.push_back(is_title_changed
                                  ? quote(before.value.title) + " -> " + quote(after.value.title)
                                  : cell(shown.title));
            }
            if (has_summary) {
                const bool is_summary_changed =
                    is_modified && (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_SUMMARY) != 0;
                row.push_back(is_summary_changed
                                  ? diffSummary(session, diff, range, painter,
                                                before.value.summary, after.value.summary)
                                  : cell(shown.summary));
            }
            for (const TagColumn& tag : tags) {
                const uint64_t now =
                    KiottySpec_readItemTagAt(session.context, textOf(diff.id), tag.index, shown_at)
                        .value.value;
                const uint64_t old =
                    is_modified ? KiottySpec_readItemTagAt(session.context, textOf(diff.id),
                                                           tag.index, range.from)
                                      .value.value
                                : now;
                row.push_back(old != now ? quote(bitNames(tag.definition, old)) + " -> " +
                                               quote(bitNames(tag.definition, now))
                                         : bitNames(tag.definition, now));
            }
            for (const auto& relation : relations) {
                const auto found = changes.find({diff.id, relation.first});
                std::string text {};
                const bool has_changes = found != changes.end();
                if (has_changes) {
                    for (const RelationChange& change : found->second) {
                        if (is_modified) {
                            text += (text.empty() ? "" : " ") +
                                    painter.mark(change.is_added, change.to_id);
                        } else {
                            text += (text.empty() ? "" : ",") + change.to_id;
                        }
                    }
                }
                row.push_back(text.empty() ? "-" : text);
            }
            printRow(row);
        }
    }
    return 0;
}

namespace {

std::optional<KiottySpecCheckpointHandle> parseCheckpointArgument(std::string_view text)
{
    const KiottySpecCheckpointHandle current {KIOTTY_SPEC_CHECKPOINT_CURRENT};
    if (text == "current" || text == "-") { return current; }
    return parseCheckpoint(text);
}

int printSummary(Session& session, const KiottySpecDiffQuery& query)
{
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(session.context, &query);
    if (summary.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, summary.code); }
    printRow({ "kind", "added", "modified", "removed", "relations_added", "relations_removed",
               "decision" });
    for (uint32_t index = 0; index < KIOTTY_SPEC_ITEM_KIND_COUNT; ++index) {
        const KiottySpecKindDiff& kind = summary.kinds[index];
        const uint32_t changes = kind.added + kind.modified + kind.removed + kind.relations_added +
                                 kind.relations_removed;
        if (changes == 0 && kind.decision_changed == KIOTTY_SPEC_FALSE) { continue; }
        printRow({ kindName(static_cast<KiottySpecItemKind>(index + 1)), std::to_string(kind.added),
                   std::to_string(kind.modified), std::to_string(kind.removed),
                   std::to_string(kind.relations_added), std::to_string(kind.relations_removed),
                   kind.decision_changed == KIOTTY_SPEC_TRUE ? "changed" : "-" });
    }
    return 0;
}

bool detectColorTerminal()
{
#ifdef _WIN32
    const bool is_terminal = _isatty(_fileno(stdout)) != 0;
    if (!is_terminal) { return false; }
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE) { return false; }
    const bool has_console_mode = GetConsoleMode(output, &mode) != 0;
    if (!has_console_mode) { return false; }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

std::string diffSummary(Session& session, const KiottySpecItemDiff& diff,
                        const KiottySpecDiffSummaryResult& range, const Painter& painter,
                        const std::string& before, const std::string& after)
{
    if (diff.item == KIOTTY_SPEC_INVALID_ITEM_HANDLE) {
        return quote(before) + " -> " + quote(after);
    }
    const KiottySpecDiffQuery points {range.from, range.to, 0, 0};
    const KiottySpecCursorResult cursor = KiottySpec_openTextDiffCursor(
        session.context, diff.item, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, &points);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        return quote(before) + " -> " + quote(after);
    }
    std::string text {};
    KiottySpecTextDiffPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchTextDiffPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            text += painter.paint(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return text.empty() ? "-" : text;
}

std::string quote(std::string_view text)
{
    std::string value = "\"";
    for (const char letter : escape(text)) {
        if (letter == '"') { value += '\\'; }
        value += letter;
    }
    return value + "\"";
}

std::string escape(std::string_view text)
{
    std::string value = cell(text);
    return text.empty() ? std::string() : value;
}

}

}
