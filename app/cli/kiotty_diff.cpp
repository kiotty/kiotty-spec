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

struct RelationChange {
    bool is_added {};
    std::string to_id {};
};

struct DiffGroup {
    KiottySpecChange change {};
    std::string_view flag {};
    std::string_view label {};
};

constexpr DiffGroup DIFF_GROUPS[] = {
    { KIOTTY_SPEC_CHANGE_ADDED, "added", "# added" },
    { KIOTTY_SPEC_CHANGE_REMOVED, "removed", "# removed" },
    { KIOTTY_SPEC_CHANGE_MODIFIED, "modified", "# modified" },
};

using RelationKey = std::pair<std::string, uint32_t>;
using RelationChanges = std::map<RelationKey, std::vector<RelationChange>>;

struct RelationChangesResult {
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
    RelationChanges changes {};
};

struct ItemDiffsResult {
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
    std::vector<KiottySpecItemDiff> diffs {};
};

struct ItemVersions {
    KiottySpecItem before {};
    KiottySpecItem after {};
};

bool detectColorTerminal();

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

    std::string mark(const RelationChange& change) const
    {
        const std::string text = (change.is_added ? "+" : "-") + change.to_id;
        if (!is_colored) { return text; }
        return std::string(change.is_added ? BLUE : RED) + text + RESET;
    }
};

struct DiffView {
    KiottySpecDiffSummaryResult range {};
    Painter painter {};
    DiffColumns columns {};
    RelationChanges changes {};
    bool has_title {};
    bool has_summary {};
};

std::optional<KiottySpecCheckpointHandle> parseCheckpointArgument(std::string_view text);
int printKindDiff(const Session& session, const KiottySpecDiffQuery& range_query,
                  const std::string& kind_name);
std::vector<DiffGroup> chooseDiffGroups(const Arguments& arguments);
RelationChangesResult readRelationChanges(const Session& session, const KiottySpecDiffQuery& query,
                                          const std::vector<RelationColumn>& relations);
ItemDiffsResult readItemDiffs(const Session& session, const KiottySpecDiffQuery& query);
void printDiffGroups(const Session& session, const DiffView& view,
                     const std::vector<DiffGroup>& groups,
                     const std::vector<KiottySpecItemDiff>& diffs);
void printDiffGroup(const Session& session, const DiffView& view, const DiffGroup& group,
                    const std::vector<KiottySpecItemDiff>& diffs);
std::vector<std::string> buildDiffHeader(const DiffView& view);
std::vector<std::string> buildDiffRow(const Session& session, const DiffView& view,
                                      const KiottySpecItemDiff& diff);
std::string describeTitle(const KiottySpecItemDiff& diff, const ItemVersions& versions);
std::string describeSummary(const Session& session, const DiffView& view,
                            const KiottySpecItemDiff& diff, const ItemVersions& versions);
std::string diffSummary(const Session& session, const DiffView& view,
                        const KiottySpecItemDiff& diff, const ItemVersions& versions);
std::string describeTagChange(const Session& session, const DiffView& view,
                              const KiottySpecItemDiff& diff, const TagColumn& tag);
std::string describeRelationChange(const DiffView& view, const KiottySpecItemDiff& diff,
                                   uint32_t relation);
bool isAfterShown(const KiottySpecItemDiff& diff);

}

int runDiff(const Session& session)
{
    const std::string* from_name = positional(session, 0);
    const std::string* to_name = positional(session, 1);
    const std::optional<KiottySpecCheckpointHandle> from =
        from_name == nullptr ? std::nullopt : parseCheckpointArgument(*from_name);
    const std::optional<KiottySpecCheckpointHandle> to =
        to_name == nullptr ? std::nullopt : parseCheckpointArgument(*to_name);
    const bool has_range = from.has_value() && to.has_value();
    if (!has_range) { return usage(findMessage(MessageKey::DIFF_USAGE)); }
    const KiottySpecDiffQuery query {*from, *to, 0, 0};
    const std::optional<std::string> kind_name = session.args.option("kind");
    if (!kind_name) { return printDiffSummary(session, query); }
    return printKindDiff(session, query, *kind_name);
}

namespace {

std::optional<KiottySpecCheckpointHandle> parseCheckpointArgument(std::string_view text)
{
    const KiottySpecCheckpointHandle current {KIOTTY_SPEC_CHECKPOINT_CURRENT};
    if (text == "current" || text == "-") { return current; }
    return parseCheckpoint(text);
}

int printKindDiff(const Session& session, const KiottySpecDiffQuery& range_query,
                  const std::string& kind_name)
{
    const std::optional<KiottySpecItemKind> kind = kindArgument(session, kind_name);
    if (!kind) { return USAGE_EXIT_CODE; }
    const std::vector<DiffGroup> groups = chooseDiffGroups(session.args);
    const bool is_group_missing = groups.empty();
    if (is_group_missing) { return usage(findMessage(MessageKey::DIFF_GROUP_USAGE)); }
    const KiottySpecDiffQuery query {range_query.from, range_query.to, 1u << (*kind - 1), 0};
    const KiottySpecDiffSummaryResult range = KiottySpec_summarizeDiff(session.context, &query);
    if (range.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, range.code); }
    const bool is_colored = !session.args.flag("no-color") && detectColorTerminal();
    const std::optional<DiffColumns> columns = planDiffColumns(session, *kind);
    if (!columns) { return USAGE_EXIT_CODE; }
    const DiffColumns& planned = *columns;
    const RelationChangesResult changes = readRelationChanges(session, query, planned.relations);
    if (changes.status != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, changes.status); }
    const ItemDiffsResult item_diffs = readItemDiffs(session, query);
    if (item_diffs.status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return fail(session, item_diffs.status);
    }
    const DiffView view { range, Painter{is_colored}, planned, changes.changes,
                          session.args.flag("title"), session.args.flag("summary") };
    printDiffGroups(session, view, groups, item_diffs.diffs);
    return 0;
}

std::vector<DiffGroup> chooseDiffGroups(const Arguments& arguments)
{
    std::vector<DiffGroup> groups {};
    for (const DiffGroup& group : DIFF_GROUPS) {
        const bool is_shown = arguments.flag(group.flag);
        if (is_shown) { groups.push_back(group); }
    }
    return groups;
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

RelationChangesResult readRelationChanges(const Session& session, const KiottySpecDiffQuery& query,
                                          const std::vector<RelationColumn>& relations)
{
    RelationChangesResult result {};
    const bool is_without_relations = relations.empty();
    if (is_without_relations) { return result; }
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationDiffCursor(session.context, &query);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        return RelationChangesResult{cursor.code, {}};
    }
    KiottySpecRelationDiffPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationDiffPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRelationDiff& diff = page.elements[index];
            const RelationKey key { diff.from_id, diff.relation };
            const bool is_added = diff.change == KIOTTY_SPEC_CHANGE_ADDED;
            result.changes.try_emplace(key);
            result.changes.at(key).push_back(RelationChange{is_added, diff.to_id});
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return result;
}

ItemDiffsResult readItemDiffs(const Session& session, const KiottySpecDiffQuery& query)
{
    const KiottySpecCursorResult cursor = KiottySpec_openItemDiffCursor(session.context, &query);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return ItemDiffsResult{cursor.code, {}}; }
    ItemDiffsResult result {};
    KiottySpecItemDiffPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchItemDiffPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        result.diffs.insert(result.diffs.end(), page.elements, page.elements + page.length);
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return result;
}

void printDiffGroups(const Session& session, const DiffView& view,
                     const std::vector<DiffGroup>& groups,
                     const std::vector<KiottySpecItemDiff>& diffs)
{
    const size_t count = groups.size();
    for (size_t index = 0; index < count; ++index) {
        if (index > 0) { std::fputc('\n', stdout); }
        printDiffGroup(session, view, groups[index], diffs);
    }
}

void printDiffGroup(const Session& session, const DiffView& view, const DiffGroup& group,
                    const std::vector<KiottySpecItemDiff>& diffs)
{
    const std::string label(group.label);
    std::printf("%s\n", label.c_str());
    printRow(buildDiffHeader(view));
    for (const KiottySpecItemDiff& diff : diffs) {
        const bool is_in_group = diff.change == group.change;
        if (is_in_group) { printRow(buildDiffRow(session, view, diff)); }
    }
}

std::vector<std::string> buildDiffHeader(const DiffView& view)
{
    std::vector<std::string> header = {"id", "kind"};
    if (view.has_title) { header.push_back("title"); }
    if (view.has_summary) { header.push_back("summary"); }
    for (const TagColumn& tag : view.columns.tags) { header.push_back(tag.definition.name); }
    for (const RelationColumn& relation : view.columns.relations) {
        header.push_back(relation.name);
    }
    return header;
}

std::vector<std::string> buildDiffRow(const Session& session, const DiffView& view,
                                      const KiottySpecItemDiff& diff)
{
    const KiottySpecText id = textOf(diff.id);
    const KiottySpecItemResult before = KiottySpec_readItemAt(session.context, id, view.range.from);
    const KiottySpecItemResult after = KiottySpec_readItemAt(session.context, id, view.range.to);
    const ItemVersions versions {before.value, after.value};
    std::vector<std::string> row = {diff.id, kindName(diff.kind)};
    if (view.has_title) { row.push_back(describeTitle(diff, versions)); }
    if (view.has_summary) { row.push_back(describeSummary(session, view, diff, versions)); }
    for (const TagColumn& tag : view.columns.tags) {
        row.push_back(describeTagChange(session, view, diff, tag));
    }
    for (const RelationColumn& relation : view.columns.relations) {
        row.push_back(describeRelationChange(view, diff, relation.index));
    }
    return row;
}

std::string describeTitle(const KiottySpecItemDiff& diff, const ItemVersions& versions)
{
    const bool is_changed = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED &&
                            (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_TITLE) != 0;
    if (is_changed) { return quote(versions.before.title) + " -> " + quote(versions.after.title); }
    const bool is_after_shown = isAfterShown(diff);
    return cell(is_after_shown ? versions.after.title : versions.before.title);
}

std::string describeSummary(const Session& session, const DiffView& view,
                            const KiottySpecItemDiff& diff, const ItemVersions& versions)
{
    const bool is_changed = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED &&
                            (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_SUMMARY) != 0;
    if (is_changed) { return diffSummary(session, view, diff, versions); }
    const bool is_after_shown = isAfterShown(diff);
    return cell(is_after_shown ? versions.after.summary : versions.before.summary);
}

std::string diffSummary(const Session& session, const DiffView& view,
                        const KiottySpecItemDiff& diff, const ItemVersions& versions)
{
    const std::string whole =
        quote(versions.before.summary) + " -> " + quote(versions.after.summary);
    if (diff.item == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return whole; }
    const KiottySpecDiffQuery points {view.range.from, view.range.to, 0, 0};
    const KiottySpecCursorResult cursor = KiottySpec_openTextDiffCursor(
        session.context, diff.item, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, &points);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return whole; }
    std::string text {};
    KiottySpecTextDiffPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchTextDiffPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            text += view.painter.paint(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return text.empty() ? "-" : text;
}

std::string describeTagChange(const Session& session, const DiffView& view,
                              const KiottySpecItemDiff& diff, const TagColumn& tag)
{
    const bool is_after_shown = isAfterShown(diff);
    const KiottySpecCheckpointHandle shown_at = is_after_shown ? view.range.to : view.range.from;
    const KiottySpecText id = textOf(diff.id);
    const uint64_t now =
        KiottySpec_readItemTagAt(session.context, id, tag.index, shown_at).value.value;
    const bool is_modified = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED;
    const uint64_t old =
        is_modified ? KiottySpec_readItemTagAt(session.context, id, tag.index, view.range.from)
                          .value.value
                    : now;
    const bool is_changed = old != now;
    if (is_changed) {
        return quote(bitNames(tag.definition, old)) + " -> " + quote(bitNames(tag.definition, now));
    }
    return bitNames(tag.definition, now);
}

std::string describeRelationChange(const DiffView& view, const KiottySpecItemDiff& diff,
                                   uint32_t relation)
{
    const RelationChanges::const_iterator found = view.changes.find({diff.id, relation});
    const bool has_changes = found != view.changes.end();
    if (!has_changes) { return "-"; }
    const bool is_modified = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED;
    const std::string separator = is_modified ? " " : ",";
    std::string text {};
    for (const RelationChange& change : found->second) {
        const std::string entry = is_modified ? view.painter.mark(change) : change.to_id;
        text += (text.empty() ? std::string() : separator) + entry;
    }
    return text.empty() ? "-" : text;
}

bool isAfterShown(const KiottySpecItemDiff& diff)
{
    return diff.change == KIOTTY_SPEC_CHANGE_ADDED || diff.change == KIOTTY_SPEC_CHANGE_MODIFIED;
}

}

}
