// ks diff (설계서 v1 5.4 "ks 출력"). 색은 이 output 층만 안다.

#include "kiotty_session.h"

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

const std::vector<std::string_view> DIFF_FLAGS = {"added", "removed", "modified", "title", "summary", "no-color"};

namespace {

constexpr const char* BLUE = "\x1b[94m";
constexpr const char* RED = "\x1b[31m";
constexpr const char* RESET = "\x1b[0m";

bool colorTerminal()
{
#ifdef _WIN32
    if (_isatty(_fileno(stdout)) == 0) { return false; }
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || GetConsoleMode(output, &mode) == 0) { return false; }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

// 탭은 공백, 줄바꿈은 \n 두 글자. 빈 값을 - 로 바꾸지 않는다.
std::string plain(std::string_view text)
{
    std::string value = cell(text);
    return text.empty() ? std::string() : value;
}

std::string quoted(std::string_view text)
{
    std::string value = "\"";
    for (const char letter : plain(text)) {
        if (letter == '"') { value += '\\'; }
        value += letter;
    }
    return value + "\"";
}

std::optional<KiottySpecCheckpointHandle> checkpointArgument(std::string_view text)
{
    if (text == "current" || text == "-") { return KIOTTY_SPEC_CHECKPOINT_CURRENT; }
    return parseCheckpoint(text);
}

struct Painter {
    bool color;

    std::string added(std::string_view text) const
    {
        return color ? std::string(BLUE) + plain(text) + RESET : "{+" + plain(text) + "+}";
    }

    std::string removed(std::string_view text) const
    {
        return color ? std::string(RED) + plain(text) + RESET : "[-" + plain(text) + "-]";
    }

    std::string mark(bool add, const std::string& id) const
    {
        const std::string text = (add ? "+" : "-") + id;
        if (!color) { return text; }
        return std::string(add ? BLUE : RED) + text + RESET;
    }
};

struct RelationChange {
    bool added;
    std::string to_id;
};

using RelationKey = std::pair<std::string, uint32_t>;

int printSummary(Session& session, const KiottySpecDiffQuery& query)
{
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(session.context, query);
    if (summary.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, summary.code); }
    printRow({"kind", "added", "modified", "removed", "relations_added", "relations_removed", "decision"});
    for (uint32_t index = 0; index < KIOTTY_SPEC_ITEM_KIND_COUNT; ++index) {
        const KiottySpecKindDiff& kind = summary.kinds[index];
        if (kind.added + kind.modified + kind.removed + kind.relations_added + kind.relations_removed + kind.decision_changed == 0) { continue; }
        printRow({KiottySpec_kindName(static_cast<KiottySpecItemKind>(index + 1)), std::to_string(kind.added), std::to_string(kind.modified),
                  std::to_string(kind.removed), std::to_string(kind.relations_added), std::to_string(kind.relations_removed),
                  kind.decision_changed != 0 ? "changed" : "-"});
    }
    return 0;
}

std::string summaryDiff(Session& session, const KiottySpecItemDiff& diff, const KiottySpecDiffSummaryResult& range, const Painter& painter,
                        const std::string& before, const std::string& after)
{
    if (diff.item == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return quoted(before) + " -> " + quoted(after); }
    const KiottySpecCursorResult cursor = KiottySpec_openTextDiffCursor(session.context, diff.item, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, range.from, range.to);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return quoted(before) + " -> " + quoted(after); }
    std::string text;
    while (true) {
        const KiottySpecTextDiffResult piece = KiottySpec_nextTextDiff(session.context, cursor.value);
        if (piece.code != KIOTTY_SPEC_STATUS_SUCCESS || piece.value.length == 0) { break; }
        const std::string_view part(piece.value.text, piece.value.length);
        if (piece.value.change == KIOTTY_SPEC_TEXT_ADDED) {
            text += painter.added(part);
        } else if (piece.value.change == KIOTTY_SPEC_TEXT_REMOVED) {
            text += painter.removed(part);
        } else {
            text += plain(part);
        }
    }
    KiottySpec_closeTextDiffCursor(session.context, cursor.value);
    return text.empty() ? "-" : text;
}

}

int runDiff(Session& session)
{
    const auto& positional = session.args.positional;
    const auto from = positional.size() > 0 ? checkpointArgument(positional[0]) : std::nullopt;
    const auto to = positional.size() > 1 ? checkpointArgument(positional[1]) : std::nullopt;
    if (!from || !to) { return usage("ks diff FROM TO [--kind K (--added|--removed|--modified)...]. 체크포인트는 CP-0003, 3, current"); }
    KiottySpecDiffQuery query {*from, *to, 0, 0};
    const auto kind_name = session.args.option("kind");
    if (!kind_name) { return printSummary(session, query); }

    const auto kind = kindArgument(*kind_name);
    if (!kind) { return usage("알 수 없는 Kind: " + *kind_name + ". `ks model` 참고"); }
    const bool groups[3] = {session.args.flag("added"), session.args.flag("removed"), session.args.flag("modified")};
    if (!groups[0] && !groups[1] && !groups[2]) { return usage("--added · --removed · --modified 중 하나 이상"); }
    query.kind_mask = 1u << (*kind - 1);
    const KiottySpecDiffSummaryResult range = KiottySpec_summarizeDiff(session.context, query);
    if (range.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, range.code); }
    const Painter painter {!session.args.flag("no-color") && colorTerminal()};

    // 열: 태그와 관계 (8.4)
    std::vector<TagColumn> tags;
    for (const std::string& name : session.args.all("tag")) {
        const auto tag = findTag(session, *kind, name);
        if (!tag) { return 2; }
        tags.push_back(*tag);
    }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, *kind);
    if (session.args.flag("tags")) {
        tags.clear();
        for (uint32_t index = 0; index < model.value.tag_count; ++index) {
            TagColumn tag {index, {}};
            KiottySpec_readTagDefinition(session.context, *kind, index, &tag.definition);
            tags.push_back(tag);
        }
    }
    std::vector<std::pair<uint32_t, std::string>> relations;
    for (const std::string& name : session.args.all("relation")) {
        const auto relation = findRelation(session, *kind, name);
        if (!relation) { return 2; }
        relations.emplace_back(*relation, name);
    }
    if (session.args.flag("relations")) {
        relations.clear();
        for (uint32_t index = 0; index < model.value.relation_count; ++index) {
            relations.emplace_back(index, KiottySpec_readRelationDefinition(session.context, *kind, index).value.name);
        }
    }
    std::map<RelationKey, std::vector<RelationChange>> changes;
    if (!relations.empty()) {
        const KiottySpecCursorResult cursor = KiottySpec_openRelationDiffCursor(session.context, query);
        if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
        KiottySpecRelationDiffPage page {};
        while (KiottySpec_fetchRelationDiffPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
            for (size_t index = 0; index < page.length; ++index) {
                const KiottySpecRelationDiff& diff = page.elements[index];
                changes[{diff.from_id, diff.relation}].push_back(RelationChange{diff.change == KIOTTY_SPEC_CHANGE_ADDED, diff.to_id});
            }
        }
        KiottySpec_closeCursor(session.context, cursor.value);
    }

    std::vector<KiottySpecItemDiff> diffs;
    const KiottySpecCursorResult cursor = KiottySpec_openItemDiffCursor(session.context, query);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    KiottySpecItemDiffPage page {};
    while (KiottySpec_fetchItemDiffPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        diffs.insert(diffs.end(), page.elements, page.elements + page.length);
    }
    KiottySpec_closeCursor(session.context, cursor.value);

    std::vector<std::string> header = {"id", "kind"};
    if (session.args.flag("title")) { header.push_back("title"); }
    if (session.args.flag("summary")) { header.push_back("summary"); }
    for (const TagColumn& tag : tags) { header.push_back(tag.definition.name); }
    for (const auto& relation : relations) { header.push_back(relation.second); }

    const KiottySpecChange order[3] = {KIOTTY_SPEC_CHANGE_ADDED, KIOTTY_SPEC_CHANGE_REMOVED, KIOTTY_SPEC_CHANGE_MODIFIED};
    const char* labels[3] = {"# added", "# removed", "# modified"};
    bool first_group = true;
    for (int group = 0; group < 3; ++group) {
        if (!groups[group]) { continue; }
        if (!first_group) { std::fputc('\n', stdout); }
        first_group = false;
        std::printf("%s\n", labels[group]);
        printRow(header);
        for (const KiottySpecItemDiff& diff : diffs) {
            if (diff.change != order[group]) { continue; }
            const bool added = diff.change == KIOTTY_SPEC_CHANGE_ADDED;
            const KiottySpecItemResult before = KiottySpec_readItemAt(session.context, textOf(diff.id), range.from);
            const KiottySpecItemResult after = KiottySpec_readItemAt(session.context, textOf(diff.id), range.to);
            const KiottySpecItem& shown = added || diff.change == KIOTTY_SPEC_CHANGE_MODIFIED ? after.value : before.value;
            const KiottySpecCheckpointHandle shown_at = added || diff.change == KIOTTY_SPEC_CHANGE_MODIFIED ? range.to : range.from;
            std::vector<std::string> row = {diff.id, KiottySpec_kindName(diff.kind)};
            const bool modified = diff.change == KIOTTY_SPEC_CHANGE_MODIFIED;
            if (session.args.flag("title")) {
                const bool changed = modified && (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_TITLE) != 0;
                row.push_back(changed ? quoted(before.value.title) + " -> " + quoted(after.value.title) : cell(shown.title));
            }
            if (session.args.flag("summary")) {
                const bool changed = modified && (diff.changed_fields & KIOTTY_SPEC_CHANGED_FIELD_SUMMARY) != 0;
                row.push_back(changed ? summaryDiff(session, diff, range, painter, before.value.summary, after.value.summary) : cell(shown.summary));
            }
            for (const TagColumn& tag : tags) {
                const uint64_t now = KiottySpec_readItemTagAt(session.context, textOf(diff.id), tag.index, shown_at).value.value;
                const uint64_t old = modified ? KiottySpec_readItemTagAt(session.context, textOf(diff.id), tag.index, range.from).value.value : now;
                row.push_back(old != now ? quoted(bitNames(tag.definition, old)) + " -> " + quoted(bitNames(tag.definition, now)) : bitNames(tag.definition, now));
            }
            for (const auto& relation : relations) {
                const auto found = changes.find({diff.id, relation.first});
                std::string text;
                if (found != changes.end()) {
                    for (const RelationChange& change : found->second) {
                        if (!modified) {
                            text += (text.empty() ? "" : ",") + change.to_id;
                        } else {
                            text += (text.empty() ? "" : " ") + painter.mark(change.added, change.to_id);
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

}
