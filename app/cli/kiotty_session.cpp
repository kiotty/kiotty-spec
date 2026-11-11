#include "kiotty_session.h"

#include "kiotty_path_utf8.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace kiotty::cli {

namespace {

constexpr std::string_view FLAG_NAMES[] = {"tags", "relations", "up", "down", "help"};
constexpr size_t OPTION_PREFIX_LENGTH = 2;
constexpr size_t CHECKPOINT_NAME_CAPACITY = 32;
constexpr uint32_t CHECKPOINT_NUMBER_MASK = 0xFFFFFFFFu;
constexpr size_t CHECKPOINT_PREFIX_LENGTH = 3;
constexpr size_t MAX_CHECKPOINT_DIGITS = 9;

bool isFlag(std::string_view name);
std::vector<std::string> split(std::string_view text, char separator);
std::string describeTextProblem(MessageKey field, KiottySpecStatus status);
std::string appendText(MessageKey sentence, std::string_view text);
std::string listNames(const std::string& names);
std::string describeCondition(KiottySpecCondition condition);
std::string listUnmetConditions(Session& session);
std::string describeCompletionFailure(Session& session);
std::string describeFailure(Session& session, const KiottySpecError& error);

}

std::optional<std::string> Arguments::option(std::string_view name) const
{
    std::optional<std::string> found {};
    for (const auto& [key, value] : options) {
        if (key == name) { found = value; }
    }
    return found;
}

std::vector<std::string> Arguments::all(std::string_view name) const
{
    std::vector<std::string> values {};
    for (const auto& [key, value] : options) {
        if (key == name) { values.push_back(value); }
    }
    return values;
}

bool Arguments::flag(std::string_view name) const
{
    for (const auto& value : flags) {
        if (value == name) { return true; }
    }
    return false;
}

Arguments parseArguments(const std::vector<std::string>& tokens, size_t first,
                         const std::vector<std::string_view>& extra_flags)
{
    Arguments arguments {};
    const size_t count = tokens.size();
    for (size_t index = first; index < count; ++index) {
        const std::string& token = tokens[index];
        const bool is_option = token.size() > OPTION_PREFIX_LENGTH &&
                               token.compare(0, OPTION_PREFIX_LENGTH, "--") == 0;
        if (is_option) {
            const std::string name = token.substr(OPTION_PREFIX_LENGTH);
            bool is_extra_flag = false;
            for (const std::string_view flag : extra_flags) {
                is_extra_flag = is_extra_flag || flag == name;
            }
            const bool is_flag = isFlag(name) || is_extra_flag || index + 1 >= count;
            if (is_flag) {
                arguments.flags.push_back(name);
            } else {
                ++index;
                arguments.options.emplace_back(name, tokens[index]);
            }
            continue;
        }
        arguments.positional.push_back(token);
    }
    return arguments;
}

int fail(Session& session, KiottySpecStatus status)
{
    std::cerr << "error: " << KiottySpec_formatStatus(status).value.value;
    const KiottySpecErrorResult last = KiottySpec_readLastError(session.context);
    const bool is_described = last.code == KIOTTY_SPEC_STATUS_SUCCESS &&
                              last.value.status == status &&
                              last.value.subject != KIOTTY_SPEC_SUBJECT_NONE;
    if (is_described) { std::cerr << ": " << describeFailure(session, last.value); }
    std::cerr << "\n";
    return 1;
}

int usage(std::string_view message)
{
    std::cerr << "error: usage: " << message << "\n";
    return USAGE_EXIT_CODE;
}

std::optional<KiottySpecItemKind> kindArgument(Session& session, std::string_view text)
{
    const KiottySpecItemKind kind = KiottySpec_parseKind(textOf(text));
    if (kind != 0) { return kind; }
    std::string names {};
    for (uint32_t value = 1;; ++value) {
        const KiottySpecItemKind candidate = static_cast<KiottySpecItemKind>(value);
        const bool has_kind = !kindName(candidate).empty();
        if (!has_kind) { break; }
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, candidate);
        if (model.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (text == model.value.name) { return candidate; }
        names += (names.empty() ? "" : ", ") + kindName(candidate) + "(" + model.value.name + ")";
    }
    std::cerr << "error: " << formatMessage(MessageKey::UNKNOWN_KIND, {text, names}) << "\n";
    return std::nullopt;
}

std::optional<TagColumn> findTag(Session& session, KiottySpecItemKind kind, std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names {};
    for (uint32_t index = 0;
         model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.tag_count; ++index) {
        TagColumn column {index, {}};
        const KiottySpecStatusResult read =
            KiottySpec_readTagDefinition(session.context, kind, index, &column.definition);
        if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (column.definition.name == name) { return column; }
        names += (names.empty() ? "" : ", ") + std::string(column.definition.name);
    }
    std::cerr << "error: "
              << formatMessage(MessageKey::UNKNOWN_TAG, {kindName(kind), name, listNames(names)})
              << "\n";
    return std::nullopt;
}

std::optional<uint32_t> findRelation(Session& session, KiottySpecItemKind kind,
                                     std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names {};
    for (uint32_t index = 0;
         model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.relation_count; ++index) {
        const KiottySpecRelationDefinitionResult definition =
            KiottySpec_readRelationDefinition(session.context, kind, index);
        if (definition.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (definition.value.name == name) { return index; }
        names += (names.empty() ? "" : ", ") + std::string(definition.value.name);
    }
    std::cerr << "error: "
              << formatMessage(MessageKey::UNKNOWN_RELATION,
                               {kindName(kind), name, listNames(names)})
              << "\n";
    return std::nullopt;
}

KiottySpecText textOf(std::string_view text) { return KiottySpecText{text.data(), text.size()}; }

std::string kindName(KiottySpecItemKind kind) { return KiottySpec_formatKind(kind).value.value; }

std::string stateName(KiottySpecCheckpointState state)
{ return KiottySpec_formatCheckpointState(state).value.value; }

std::string cell(std::string_view value)
{
    std::string text {};
    text.reserve(value.size());
    for (const char letter : value) {
        if (letter == '\t') {
            text += ' ';
        } else if (letter == '\n') {
            text += "\\n";
        } else if (letter != '\r') {
            text += letter;
        }
    }
    return text.empty() ? "-" : text;
}

void printRow(const std::vector<std::string>& cells)
{
    std::string line {};
    const size_t count = cells.size();
    for (size_t index = 0; index < count; ++index) {
        if (index > 0) { line += '\t'; }
        line += cells[index];
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
}

std::string checkpointName(KiottySpecCheckpointHandle handle)
{
    if (handle == 0) { return "-"; }
    char text[CHECKPOINT_NAME_CAPACITY] {};
    std::snprintf(text, sizeof(text), "CP-%04llu",
                  static_cast<unsigned long long>(handle & CHECKPOINT_NUMBER_MASK));
    return text;
}

std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text)
{
    const std::string_view prefix = text.substr(0, CHECKPOINT_PREFIX_LENGTH);
    const bool has_prefix =
        text.size() > CHECKPOINT_PREFIX_LENGTH && (prefix == "CP-" || prefix == "cp-");
    if (has_prefix) { text.remove_prefix(CHECKPOINT_PREFIX_LENGTH); }
    const bool is_wrong_length = text.empty() || text.size() > MAX_CHECKPOINT_DIGITS;
    if (is_wrong_length) { return std::nullopt; }
    uint64_t value = 0;
    for (const char letter : text) {
        if (letter < '0' || letter > '9') { return std::nullopt; }
        value = value * DECIMAL_BASE + static_cast<uint64_t>(letter - '0');
    }
    if (value == 0) { return std::nullopt; }
    return value;
}

std::optional<std::string> readFileText(const std::string& path)
{
    std::ifstream stream(pathFromUtf8(path), std::ios::binary);
    if (!stream) { return std::nullopt; }
    std::ostringstream content {};
    content << stream.rdbuf();
    return content.str();
}

std::string itemId(Session& session, KiottySpecItemHandle handle)
{
    if (handle == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return "-"; }
    const KiottySpecItemResult item = KiottySpec_readItemByHandle(session.context, handle);
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return "-"; }
    return item.value.id;
}

std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition, std::string_view names)
{
    if (names == "-") { return uint64_t{0}; }
    uint64_t value = 0;
    for (const std::string& name : split(names, ',')) {
        bool has_match = false;
        for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
            if (definition.bit_names[bit][0] != '\0' && name == definition.bit_names[bit]) {
                value |= uint64_t{1} << bit;
                has_match = true;
            }
        }
        if (!has_match) {
            std::string known {};
            for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
                if (definition.bit_names[bit][0] != '\0') {
                    known += (known.empty() ? "" : ", ") + std::string(definition.bit_names[bit]);
                }
            }
            std::cerr << "error: "
                      << formatMessage(MessageKey::UNKNOWN_TAG_VALUE,
                                       {definition.name, name, known})
                      << "\n";
            return std::nullopt;
        }
    }
    return value;
}

std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value)
{
    std::string text {};
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        const bool has_text = !text.empty();
        if (has_text) { text += '|'; }
        text +=
            definition.bit_names[bit][0] != '\0' ? definition.bit_names[bit] : std::to_string(bit);
    }
    return text.empty() ? "-" : text;
}

namespace {

bool isFlag(std::string_view name)
{
    for (const std::string_view flag : FLAG_NAMES) {
        if (flag == name) { return true; }
    }
    return false;
}

std::string describeFailure(Session& session, const KiottySpecError& error)
{
    using Key = MessageKey;
    const std::string line_number = std::to_string(error.line);
    const std::string line =
        error.line > 0 ? formatMessage(Key::FAILURE_LINE, {line_number}) : std::string();
    const KiottySpecStatus status = error.status;
    const std::string_view text(error.text);
    switch (error.subject) {
    case KIOTTY_SPEC_SUBJECT_NONE: return std::string();
    case KIOTTY_SPEC_SUBJECT_FILE: return line + appendText(Key::FILE_UNUSABLE, text);
    case KIOTTY_SPEC_SUBJECT_FILE_LINE:
        return line + appendText(Key::FILE_LINE_INVALID, text);
    case KIOTTY_SPEC_SUBJECT_MERGE_MARKER:
        return line + std::string(findMessage(Key::MERGE_MARKER_LEFT));
    case KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE:
        return line + appendText(Key::DUPLICATE_LINE, text);
    case KIOTTY_SPEC_SUBJECT_FILE_CHANGED: return appendText(Key::FILE_CHANGED, text);
    case KIOTTY_SPEC_SUBJECT_FILE_FORMAT:
        return line + std::string(findMessage(Key::FILE_FORMAT_MISMATCH));
    case KIOTTY_SPEC_SUBJECT_CONFIGURATION:
        return line + appendText(Key::CAPACITY_REACHED, text);
    case KIOTTY_SPEC_SUBJECT_MODEL: return std::string(findMessage(Key::MODEL_UNLOADABLE));
    case KIOTTY_SPEC_SUBJECT_KIND: return std::string(findMessage(Key::KIND_INVALID));
    case KIOTTY_SPEC_SUBJECT_ITEM:
        return text.empty() ? std::string(findMessage(Key::ITEM_HANDLE_INVALID))
                            : appendText(Key::ITEM_MISSING, text);
    case KIOTTY_SPEC_SUBJECT_ID:
        if (status == KIOTTY_SPEC_STATUS_CONFLICT) { return appendText(Key::ID_TAKEN, text); }
        return appendText(Key::ID_INVALID, text);
    case KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY: return appendText(Key::ID_IN_HISTORY, text);
    case KIOTTY_SPEC_SUBJECT_TITLE: return describeTextProblem(Key::FIELD_TITLE, status);
    case KIOTTY_SPEC_SUBJECT_SUMMARY: return describeTextProblem(Key::FIELD_SUMMARY, status);
    case KIOTTY_SPEC_SUBJECT_BODY: return describeTextProblem(Key::FIELD_BODY, status);
    case KIOTTY_SPEC_SUBJECT_PAST_BODY: return appendText(Key::PAST_BODY_IN_GIT, text);
    case KIOTTY_SPEC_SUBJECT_PARENT: return appendText(Key::PARENT_CYCLE, text);
    case KIOTTY_SPEC_SUBJECT_CHILD: return appendText(Key::CHILD_EXISTS, text);
    case KIOTTY_SPEC_SUBJECT_TAG: return line + appendText(Key::TAG_NOT_IN_MODEL, text);
    case KIOTTY_SPEC_SUBJECT_TAG_VALUE:
        return line + appendText(Key::TAG_VALUE_INVALID, text);
    case KIOTTY_SPEC_SUBJECT_RELATION:
        if (status == KIOTTY_SPEC_STATUS_CONFLICT) { return appendText(Key::RELATION_TAKEN, text); }
        if (status == KIOTTY_SPEC_STATUS_NOT_FOUND) {
            return appendText(Key::RELATION_MISSING, text);
        }
        return line + appendText(Key::RELATION_NOT_IN_MODEL, text);
    case KIOTTY_SPEC_SUBJECT_DECISION: return describeTextProblem(Key::FIELD_DECISION, status);
    case KIOTTY_SPEC_SUBJECT_CHECKPOINT:
        if (status == KIOTTY_SPEC_STATUS_CONFIRMED) {
            return std::string(findMessage(Key::CHECKPOINT_DONE));
        }
        if (status == KIOTTY_SPEC_STATUS_PRECONDITION) {
            return std::string(findMessage(Key::CHECKPOINT_PRECONDITION));
        }
        return std::string(findMessage(Key::CHECKPOINT_MISSING));
    case KIOTTY_SPEC_SUBJECT_DESCRIPTION:
        return describeTextProblem(Key::FIELD_DESCRIPTION, status);
    case KIOTTY_SPEC_SUBJECT_STEP: return std::string(findMessage(Key::STEP_RANGE));
    case KIOTTY_SPEC_SUBJECT_ACTOR: return std::string(findMessage(Key::USER_ONLY));
    case KIOTTY_SPEC_SUBJECT_CURSOR: return std::string(findMessage(Key::CURSOR_LIMIT));
    case KIOTTY_SPEC_SUBJECT_CURSOR_ROWS: return std::string(findMessage(Key::CURSOR_ROWS_LIMIT));
    case KIOTTY_SPEC_SUBJECT_DIFF_RANGE: return std::string(findMessage(Key::DIFF_RANGE_INVALID));
    case KIOTTY_SPEC_SUBJECT_TEXT_FIELD: return std::string(findMessage(Key::TEXT_DIFF_FIELD));
    case KIOTTY_SPEC_SUBJECT_PROJECT_NAME:
        return describeTextProblem(Key::FIELD_PROJECT_NAME, status);
    case KIOTTY_SPEC_SUBJECT_COMPLETION: return describeCompletionFailure(session);
    }
    return std::string();
}

std::string describeTextProblem(MessageKey field, KiottySpecStatus status)
{
    const std::string_view name = findMessage(field);
    if (status == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        return formatMessage(MessageKey::TEXT_TOO_LONG, {name});
    }
    if (status == KIOTTY_SPEC_STATUS_INVALID_UTF8) {
        return formatMessage(MessageKey::TEXT_NOT_UTF8, {name});
    }
    return formatMessage(MessageKey::TEXT_EMPTY_OR_NULL, {name});
}

std::string appendText(MessageKey sentence, std::string_view text)
{
    const std::string message(findMessage(sentence));
    return text.empty() ? message : message + ": " + std::string(text);
}

std::string listNames(const std::string& names)
{
    const bool has_names = !names.empty();
    return has_names ? names : std::string(findMessage(MessageKey::NOTHING_LISTED));
}

std::string describeCompletionFailure(Session& session)
{
    const KiottySpecCheckpointResult checkpoint =
        KiottySpec_readCheckpoint(session.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const std::string step = std::to_string(static_cast<int>(checkpoint.value.step));
    const std::string failure =
        checkpoint.code == KIOTTY_SPEC_STATUS_SUCCESS
            ? formatMessage(MessageKey::COMPLETION_UNMET_AT_STEP, {step})
            : std::string(findMessage(MessageKey::COMPLETION_UNMET));
    return failure + listUnmetConditions(session);
}

std::string listUnmetConditions(Session& session)
{
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(session.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return std::string(); }
    std::string text {};
    bool is_first = true;
    KiottySpecCondition current = KIOTTY_SPEC_CONDITION_ENTRY_STEP;
    KiottySpecUnmetConditionPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchUnmetConditionPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecUnmetCondition& unmet = page.elements[index];
            if (is_first || unmet.condition != current) {
                text += "\n- " + describeCondition(unmet.condition) + ":";
            }
            is_first = false;
            current = unmet.condition;
            text += std::string(" ") + unmet.id;
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return text;
}

std::string describeCondition(KiottySpecCondition condition)
{
    using Key = MessageKey;
    switch (condition) {
    case KIOTTY_SPEC_CONDITION_ENTRY_STEP:
        return std::string(findMessage(Key::CONDITION_ENTRY_STEP));
    case KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA:
        return std::string(findMessage(Key::CONDITION_ACCEPTANCE_CRITERIA));
    case KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE:
        return std::string(findMessage(Key::CONDITION_MILESTONE_COVERAGE));
    case KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR:
        return std::string(findMessage(Key::CONDITION_SYSTEM_TEST_PAIR));
    case KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR:
        return std::string(findMessage(Key::CONDITION_END_TO_END_TEST_PAIR));
    case KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR:
        return std::string(findMessage(Key::CONDITION_CODE_INSPECTION_PAIR));
    case KIOTTY_SPEC_CONDITION_SINGLE_TARGET:
        return std::string(findMessage(Key::CONDITION_SINGLE_TARGET));
    case KIOTTY_SPEC_CONDITION_PASSING_RESULT:
        return std::string(findMessage(Key::CONDITION_PASSING_RESULT));
    case KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST:
        return std::string(findMessage(Key::CONDITION_CLOSED_CHANGE_REQUEST));
    }
    return std::to_string(static_cast<int>(condition));
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts {};
    size_t start = 0;
    const size_t length = text.size();
    while (start <= length) {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos) { end = text.size(); }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}

}
