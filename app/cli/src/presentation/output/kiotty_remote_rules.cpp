#include "presentation/commands/kiotty_remote.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace kiotty::cli {

namespace {

constexpr size_t SHORT_COMMIT_LENGTH = 7;
constexpr size_t TABLE_ROW_RESERVE = 512;

bool isWanted(const std::vector<std::string>& wanted, const std::string& id);
bool isBodyDiffShown(const RuleDiffRow& row);
int printBodyDiff(const Session& local, const Session& remote, const std::string& id);
std::string formatTextDiffPage(const KiottySpecTextDiffPage& page, char previous);
std::string markLines(std::string_view value, char mark, char previous);
char markOf(KiottySpecTextChange change);
AcceptImpact collectRuleImpact(const Session& session, const RuleDiffRow& row);
std::vector<KiottySpecRelation> readIncoming(const Session& session, const std::string& id);
std::string buildPrivateTable(const AcceptImpact& impact);
std::string buildRulesTable(const std::vector<RuleDiffRow>& rows, size_t limit);
std::string escapeTableCell(std::string_view text);
std::string formatChange(KiottySpecChange change);
std::string formatFields(uint32_t fields);

}

std::optional<std::vector<RuleDiffRow>> readRuleDiffs(const Session& local, const Session& remote)
{
    const KiottySpecCursorResult cursor =
        KiottySpec_openRuleDiffCursor(&local.context, &remote.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(local, cursor.code);
        return std::nullopt;
    }
    std::vector<RuleDiffRow> rows {};
    const std::unique_ptr<KiottySpecRuleDiffPage> page_storage =
        std::make_unique<KiottySpecRuleDiffPage>();
    KiottySpecRuleDiffPage& page = *page_storage;
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    while (true) {
        status = KiottySpec_fetchRuleDiffPage(&local.context, cursor.value, &page).code;
        if (status != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRuleDiff& diff = page.elements[index];
            rows.push_back(RuleDiffRow{diff.id, diff.title, diff.change, diff.changed_fields});
        }
    }
    KiottySpec_closeCursor(&local.context, cursor.value);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(local, status);
        return std::nullopt;
    }
    return rows;
}

std::vector<RuleDiffRow> filterRuleDiffs(const std::vector<RuleDiffRow>& rows,
                                         const std::vector<std::string>& wanted)
{
    std::vector<RuleDiffRow> shown {};
    for (const RuleDiffRow& row : rows) {
        const bool is_shown = wanted.empty() || isWanted(wanted, row.id);
        if (is_shown) { shown.push_back(row); }
    }
    return shown;
}

int printRuleDiffs(const Session& local, const Session& remote,
                   const std::vector<RuleDiffRow>& rows)
{
    printRow({"id", "change", "fields", "title"});
    for (const RuleDiffRow& row : rows) {
        printRow({row.id, formatChange(row.change), formatFields(row.fields), cell(row.title)});
    }
    const bool has_body_flag = local.arguments.flag("body");
    if (!has_body_flag) { return 0; }
    for (const RuleDiffRow& row : rows) {
        const bool is_shown = isBodyDiffShown(row);
        const int printed = is_shown ? printBodyDiff(local, remote, row.id) : 0;
        if (printed != 0) { return printed; }
    }
    return 0;
}

AcceptImpact collectImpact(const Session& session, const std::vector<RuleDiffRow>& rows)
{
    AcceptImpact impact {};
    for (const RuleDiffRow& row : rows) {
        const bool is_local = row.change != KIOTTY_SPEC_CHANGE_ADDED;
        const AcceptImpact found = is_local ? collectRuleImpact(session, row) : AcceptImpact{};
        impact.private_rules.insert(impact.private_rules.end(), found.private_rules.begin(),
                                    found.private_rules.end());
        impact.referrers.insert(impact.referrers.end(), found.referrers.begin(),
                                found.referrers.end());
    }
    return impact;
}

AcceptTexts buildAcceptTexts(const KiottySpecRemote& remote, const std::string& commit,
                             const std::vector<RuleDiffRow>& rows, const AcceptImpact& impact)
{
    const DiffCounts counts = countDiffs(rows);
    const std::string prefix = formatShortCommit(commit);
    const std::string remote_section =
        formatMessage(MessageKey::ACCEPT_BODY_REMOTE, {remote.url, commit});
    const std::string private_table = buildPrivateTable(impact);
    const size_t used = remote_section.size() + private_table.size() + TABLE_ROW_RESERVE;
    const bool has_room = used < KIOTTY_SPEC_MAX_BODY_LENGTH;
    const size_t limit = has_room ? KIOTTY_SPEC_MAX_BODY_LENGTH - used : 0;
    AcceptTexts texts {};
    texts.description = formatMessage(MessageKey::ACCEPT_DESCRIPTION, {prefix});
    texts.title = formatMessage(MessageKey::ACCEPT_TITLE, {prefix});
    texts.summary = formatMessage(MessageKey::ACCEPT_SUMMARY,
                                  {std::to_string(counts.added), std::to_string(counts.modified),
                                   std::to_string(counts.removed)});
    texts.body = remote_section + buildRulesTable(rows, limit) + private_table;
    texts.rule_base = commit;
    return texts;
}

void printAcceptResult(const KiottySpecAcceptResult& accepted, const AcceptImpact& impact)
{
    printRow({"checkpoint", "change_request"});
    printRow({checkpointName(accepted.checkpoint), accepted.change_request});
    printRow({});
    printRow({"item", "relation", "rule", "result"});
    for (const ReferrerRow& row : impact.referrers) {
        const std::string result = row.is_deleted ? "deleted" : "unlinked";
        printRow({row.item, row.relation, row.rule, result});
    }
    printRow({});
    printRow({"private_rule", "rule", "change", "fields"});
    for (const PrivateRuleRow& row : impact.private_rules) {
        const RuleDiffRow& rule = row.rule;
        printRow({row.private_rule, rule.id, formatChange(rule.change), formatFields(rule.fields)});
    }
}

DiffCounts countDiffs(const std::vector<RuleDiffRow>& rows)
{
    DiffCounts counts {};
    for (const RuleDiffRow& row : rows) {
        switch (row.change) {
        case KIOTTY_SPEC_CHANGE_ADDED: ++counts.added; break;
        case KIOTTY_SPEC_CHANGE_MODIFIED: ++counts.modified; break;
        case KIOTTY_SPEC_CHANGE_REMOVED: ++counts.removed; break;
        }
    }
    return counts;
}

std::string formatShortCommit(std::string_view commit)
{
    const bool is_empty = commit.empty();
    if (is_empty) { return "-"; }
    return std::string(commit.substr(0, SHORT_COMMIT_LENGTH));
}

namespace {

bool isWanted(const std::vector<std::string>& wanted, const std::string& id)
{
    return std::find(wanted.begin(), wanted.end(), id) != wanted.end();
}

int printBodyDiff(const Session& local, const Session& remote, const std::string& id)
{
    const KiottySpecCursorResult cursor =
        KiottySpec_openRuleBodyDiffCursor(&local.context, &remote.context, textOf(id));
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(local, cursor.code); }
    std::string text = "@@ " + id + "\n";
    const std::unique_ptr<KiottySpecTextDiffPage> page_storage =
        std::make_unique<KiottySpecTextDiffPage>();
    KiottySpecTextDiffPage& page = *page_storage;
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    while (true) {
        status = KiottySpec_fetchTextDiffPage(&local.context, cursor.value, &page).code;
        if (status != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        text += formatTextDiffPage(page, text.back());
    }
    KiottySpec_closeCursor(&local.context, cursor.value);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(local, status); }
    const bool is_open_line = text.back() != '\n';
    if (is_open_line) { text += '\n'; }
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}

std::string formatTextDiffPage(const KiottySpecTextDiffPage& page, char previous)
{
    std::string text {};
    char last = previous;
    for (size_t index = 0; index < page.length; ++index) {
        const KiottySpecTextDiff& piece = page.elements[index];
        const std::string_view value(piece.text, piece.length);
        text += markLines(value, markOf(piece.change), last);
        last = value.empty() ? last : value.back();
    }
    return text;
}

std::string markLines(std::string_view value, char mark, char previous)
{
    std::string text {};
    bool is_line_start = previous == '\n';
    for (const char letter : value) {
        if (is_line_start) { text += mark; }
        text += letter;
        is_line_start = letter == '\n';
    }
    return text;
}

char markOf(KiottySpecTextChange change)
{
    switch (change) {
    case KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED: return ' ';
    case KIOTTY_SPEC_TEXT_CHANGE_ADDED: return '+';
    case KIOTTY_SPEC_TEXT_CHANGE_REMOVED: return '-';
    }
    return ' ';
}

AcceptImpact collectRuleImpact(const Session& session, const RuleDiffRow& row)
{
    AcceptImpact impact {};
    const std::vector<KiottySpecRelation> incoming = readIncoming(session, row.id);
    const bool is_removed = row.change == KIOTTY_SPEC_CHANGE_REMOVED;
    const std::string_view replacing = findMessage(MessageKey::RELATION_REPLACED_RULE);
    const std::string_view inspecting = findMessage(MessageKey::RELATION_INSPECTED_RULE);
    for (const KiottySpecRelation& relation : incoming) {
        const std::string name = findRelationName(session, relation.model, relation.relation);
        const std::string from = itemId(session, relation.from);
        const bool is_replacing =
            relation.model == KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE && name == replacing;
        if (is_replacing) { impact.private_rules.push_back(PrivateRuleRow{from, row}); }
        const bool is_result =
            relation.model == KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT && name == inspecting;
        const bool is_listed = is_result || is_removed;
        if (is_listed) { impact.referrers.push_back(ReferrerRow{from, name, row.id, is_result}); }
    }
    return impact;
}

std::vector<KiottySpecRelation> readIncoming(const Session& session, const std::string& id)
{
    std::vector<KiottySpecRelation> relations {};
    const KiottySpecItemHandleResult rule = KiottySpec_findItem(&session.context, textOf(id));
    if (rule.code != KIOTTY_SPEC_STATUS_SUCCESS) { return relations; }
    const KiottySpecCursorResult cursor = KiottySpec_openRelationCursor(
        &session.context, textOf(id), KIOTTY_SPEC_DIRECTION_BOTH);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return relations; }
    const std::unique_ptr<KiottySpecRelationPage> page_storage =
        std::make_unique<KiottySpecRelationPage>();
    KiottySpecRelationPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            relations.push_back(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    std::vector<KiottySpecRelation> incoming {};
    for (const KiottySpecRelation& relation : relations) {
        const bool is_incoming = relation.to == rule.value;
        if (is_incoming) { incoming.push_back(relation); }
    }
    return incoming;
}

std::string buildPrivateTable(const AcceptImpact& impact)
{
    const std::string pending(findMessage(MessageKey::ACCEPT_PENDING));
    std::string text(findMessage(MessageKey::ACCEPT_BODY_PRIVATE_RULES));
    for (const PrivateRuleRow& row : impact.private_rules) {
        const RuleDiffRow& rule = row.rule;
        text += "| " + row.private_rule + " | " + rule.id + " | " + formatChange(rule.change) +
                " | " + formatFields(rule.fields) + " | " + pending + " | |\n";
    }
    return text;
}

std::string buildRulesTable(const std::vector<RuleDiffRow>& rows, size_t limit)
{
    std::string text(findMessage(MessageKey::ACCEPT_BODY_RULES));
    size_t written = 0;
    for (const RuleDiffRow& row : rows) {
        const std::string line = "| " + row.id + " | " + formatChange(row.change) + " | " +
                                 formatFields(row.fields) + " | " + escapeTableCell(row.title) +
                                 " |\n";
        const bool is_full = text.size() + line.size() > limit;
        if (is_full) { break; }
        text += line;
        ++written;
    }
    const size_t omitted = rows.size() - written;
    const bool has_omitted = omitted > 0;
    if (has_omitted) { text += "| ... | +" + std::to_string(omitted) + " | | |\n"; }
    return text;
}

std::string escapeTableCell(std::string_view text)
{
    std::string value {};
    for (const char letter : cell(text)) {
        if (letter == '|') { value += '\\'; }
        value += letter;
    }
    return value;
}

bool isBodyDiffShown(const RuleDiffRow& row)
{
    const bool is_body_changed = (row.fields & KIOTTY_SPEC_CHANGED_FIELD_BODY) != 0;
    return is_body_changed || row.change != KIOTTY_SPEC_CHANGE_MODIFIED;
}

std::string formatChange(KiottySpecChange change)
{
    switch (change) {
    case KIOTTY_SPEC_CHANGE_ADDED: return "added";
    case KIOTTY_SPEC_CHANGE_MODIFIED: return "modified";
    case KIOTTY_SPEC_CHANGE_REMOVED: return "removed";
    }
    return "-";
}

std::string formatFields(uint32_t fields)
{
    const std::pair<uint32_t, std::string_view> names[] = {
        {KIOTTY_SPEC_CHANGED_FIELD_TITLE, "title"},
        {KIOTTY_SPEC_CHANGED_FIELD_SUMMARY, "summary"},
        {KIOTTY_SPEC_CHANGED_FIELD_PARENT, "parent"},
        {KIOTTY_SPEC_CHANGED_FIELD_TAGS, "tag"},
        {KIOTTY_SPEC_CHANGED_FIELD_BODY, "body"},
    };
    std::string text {};
    for (const auto& [bit, name] : names) {
        const bool is_changed = (fields & bit) != 0;
        const std::string separator = text.empty() ? "" : ",";
        if (is_changed) { text += separator + std::string(name); }
    }
    return text.empty() ? "-" : text;
}

}

}
