#include "presentation/commands/kiotty_commands.h"

#include <iostream>
#include <memory>

namespace kiotty::cli {

namespace {

constexpr MessageKey STEP_NAME_KEYS[KIOTTY_SPEC_STEP_COUNT] = {
    MessageKey::STEP_CHANGE_REQUEST,       MessageKey::STEP_REQUIREMENT,
    MessageKey::STEP_EXTERNAL_INTERFACE,   MessageKey::STEP_DESIGN,
    MessageKey::STEP_UNIT_TEST_PLAN,       MessageKey::STEP_DEVELOPMENT_PLAN,
    MessageKey::STEP_IMPLEMENTATION,       MessageKey::STEP_SYSTEM_TEST_PLAN,
    MessageKey::STEP_SYSTEM_TEST_RUN,      MessageKey::STEP_CODE_INSPECTION,
    MessageKey::STEP_END_TO_END_TEST_PLAN, MessageKey::STEP_END_TO_END_TEST_RUN
};

enum class FailureForm { MESSAGE, TEXT, FIELD };
enum class LineMention { OMITTED, INCLUDED };

struct FailureSentence {
    KiottySpecSubject subject {};
    MessageKey key {};
    FailureForm form {};
    LineMention line {LineMention::OMITTED};
    std::optional<KiottySpecStatus> status {};
};

using Key = MessageKey;
using Form = FailureForm;
constexpr LineMention WITH_LINE = LineMention::INCLUDED;
constexpr LineMention WITHOUT_LINE = LineMention::OMITTED;

constexpr FailureSentence FAILURE_SENTENCES[] = {
    { KIOTTY_SPEC_SUBJECT_FILE, Key::FILE_UNUSABLE, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_FILE_LINE, Key::FILE_LINE_INVALID, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_MERGE_MARKER, Key::MERGE_MARKER_LEFT, Form::MESSAGE, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE, Key::DUPLICATE_LINE, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_FILE_CHANGED, Key::FILE_CHANGED, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_FILE_FORMAT, Key::FILE_FORMAT_MISMATCH, Form::MESSAGE, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_CONFIGURATION, Key::CAPACITY_REACHED, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_MODEL, Key::MODEL_UNLOADABLE, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_KIND, Key::KIND_INVALID, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_ITEM, Key::ITEM_MISSING, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_ID, Key::ID_TAKEN, Form::TEXT, WITHOUT_LINE,
      KIOTTY_SPEC_STATUS_CONFLICT },
    { KIOTTY_SPEC_SUBJECT_ID, Key::ID_INVALID, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY, Key::ID_IN_HISTORY, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_TITLE, Key::FIELD_TITLE, Form::FIELD },
    { KIOTTY_SPEC_SUBJECT_SUMMARY, Key::FIELD_SUMMARY, Form::FIELD },
    { KIOTTY_SPEC_SUBJECT_BODY, Key::FIELD_BODY, Form::FIELD },
    { KIOTTY_SPEC_SUBJECT_PAST_BODY, Key::PAST_BODY_IN_GIT, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_PARENT, Key::PARENT_CYCLE, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_CHILD, Key::CHILD_EXISTS, Form::TEXT },
    { KIOTTY_SPEC_SUBJECT_TAG, Key::TAG_NOT_IN_MODEL, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_TAG_VALUE, Key::TAG_VALUE_INVALID, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_RELATION, Key::RELATION_TAKEN, Form::TEXT, WITHOUT_LINE,
      KIOTTY_SPEC_STATUS_CONFLICT },
    { KIOTTY_SPEC_SUBJECT_RELATION, Key::RELATION_MISSING, Form::TEXT, WITHOUT_LINE,
      KIOTTY_SPEC_STATUS_NOT_FOUND },
    { KIOTTY_SPEC_SUBJECT_RELATION, Key::RELATION_NOT_IN_MODEL, Form::TEXT, WITH_LINE },
    { KIOTTY_SPEC_SUBJECT_DECISION, Key::FIELD_DECISION, Form::FIELD },
    { KIOTTY_SPEC_SUBJECT_CHECKPOINT, Key::CHECKPOINT_DONE, Form::MESSAGE, WITHOUT_LINE,
      KIOTTY_SPEC_STATUS_CONFIRMED },
    { KIOTTY_SPEC_SUBJECT_CHECKPOINT, Key::CHECKPOINT_PRECONDITION, Form::MESSAGE, WITHOUT_LINE,
      KIOTTY_SPEC_STATUS_PRECONDITION },
    { KIOTTY_SPEC_SUBJECT_CHECKPOINT, Key::CHECKPOINT_MISSING, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_DESCRIPTION, Key::FIELD_DESCRIPTION, Form::FIELD },
    { KIOTTY_SPEC_SUBJECT_STEP, Key::STEP_RANGE, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_ACTOR, Key::USER_ONLY, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_CURSOR, Key::CURSOR_LIMIT, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_CURSOR_ROWS, Key::CURSOR_ROWS_LIMIT, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_DIFF_RANGE, Key::DIFF_RANGE_INVALID, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_TEXT_FIELD, Key::TEXT_DIFF_FIELD, Form::MESSAGE },
    { KIOTTY_SPEC_SUBJECT_PROJECT_NAME, Key::FIELD_PROJECT_NAME, Form::FIELD },
};

std::string describeFailure(const Session& session, const KiottySpecError& error);
std::optional<FailureSentence> findFailureSentence(const KiottySpecError& error);
std::string writeFailureSentence(const FailureSentence& sentence, const KiottySpecError& error);
std::string describeTextProblem(MessageKey field, KiottySpecStatus status);
std::string appendText(MessageKey sentence, std::string_view text);
std::string describeCompletionFailure(const Session& session);
std::string listUnmetConditions(const Session& session);
std::string describeUnmetCondition(const KiottySpecUnmetCondition& unmet,
                                   std::optional<KiottySpecCondition> previous);
std::string describeCondition(KiottySpecCondition condition);

}

int check(const Session& session, KiottySpecStatus status)
{
    return status == KIOTTY_SPEC_STATUS_SUCCESS ? 0 : fail(session, status);
}

int fail(const Session& session, KiottySpecStatus status)
{
    std::cerr << "error: " << KiottySpec_formatStatus(status).value.value;
    const KiottySpecErrorResult last = KiottySpec_readLastError(&session.context);
    const bool is_described = last.code == KIOTTY_SPEC_STATUS_SUCCESS &&
                              last.value.status == status &&
                              last.value.subject != KIOTTY_SPEC_SUBJECT_NONE;
    if (is_described) { std::cerr << ": " << describeFailure(session, last.value); }
    std::cerr << "\n";
    return 1;
}

int failWithoutContext(KiottySpecStatus status)
{
    std::cerr << "error: " << KiottySpec_formatStatus(status).value.value << "\n";
    return 1;
}

std::optional<std::string> readPositional(const Session& session, size_t index)
{
    const std::vector<std::string>& values = session.arguments.positional;
    const bool is_present = index < values.size();
    if (!is_present) { return std::nullopt; }
    return values.at(index);
}

std::optional<uint32_t> readNumberOption(const Session& session, std::string_view name,
                                         uint32_t fallback)
{
    const std::optional<std::string> text = session.arguments.option(name);
    if (!text) { return fallback; }
    return parseNumber(*text);
}

std::string stepName(uint32_t step)
{
    for (uint32_t index = 0; index < KIOTTY_SPEC_STEP_COUNT; ++index) {
        const bool is_step = index + 1 == step;
        if (is_step) {
            const std::string name(findMessage(STEP_NAME_KEYS[index]));
            return std::to_string(step) + " " + name;
        }
    }
    return "-";
}

BodyOption readBodyOption(const Session& session, std::string_view text_name,
                          std::string_view file_name)
{
    const std::optional<std::string> text = session.arguments.option(text_name);
    if (text) { return BodyOption{text, false}; }
    const std::optional<std::string> file = session.arguments.option(file_name);
    if (file) {
        const std::optional<std::string> content = readFileText(*file);
        const bool is_unreadable = !content.has_value();
        if (is_unreadable) {
            std::cerr << "error: " << formatMessage(MessageKey::FILE_UNREADABLE, {*file}) << "\n";
        }
        return BodyOption{content, is_unreadable};
    }
    return BodyOption{};
}

namespace {

std::string describeFailure(const Session& session, const KiottySpecError& error)
{
    const bool is_completion = error.subject == KIOTTY_SPEC_SUBJECT_COMPLETION;
    if (is_completion) { return describeCompletionFailure(session); }
    const std::string_view text(error.text);
    const bool is_invalid_handle = error.subject == KIOTTY_SPEC_SUBJECT_ITEM && text.empty();
    if (is_invalid_handle) { return std::string(findMessage(MessageKey::ITEM_HANDLE_INVALID)); }
    const std::optional<FailureSentence> sentence = findFailureSentence(error);
    return sentence ? writeFailureSentence(*sentence, error) : std::string();
}

std::optional<FailureSentence> findFailureSentence(const KiottySpecError& error)
{
    for (const FailureSentence& sentence : FAILURE_SENTENCES) {
        const bool is_subject = sentence.subject == error.subject;
        const bool is_status = sentence.status.value_or(error.status) == error.status;
        if (is_subject && is_status) { return sentence; }
    }
    return std::nullopt;
}

std::string writeFailureSentence(const FailureSentence& sentence, const KiottySpecError& error)
{
    const std::string line_number = std::to_string(error.line);
    const bool has_line = sentence.line == LineMention::INCLUDED && error.line > 0;
    const std::string line =
        has_line ? formatMessage(MessageKey::FAILURE_LINE, {line_number}) : std::string();
    switch (sentence.form) {
    case FailureForm::MESSAGE: return line + std::string(findMessage(sentence.key));
    case FailureForm::TEXT: return line + appendText(sentence.key, error.text);
    case FailureForm::FIELD: return line + describeTextProblem(sentence.key, error.status);
    }
    return line;
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

std::string describeCompletionFailure(const Session& session)
{
    const KiottySpecCheckpointResult checkpoint =
        KiottySpec_readCheckpoint(&session.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const std::string step = std::to_string(static_cast<int>(checkpoint.value.step));
    const std::string failure =
        checkpoint.code == KIOTTY_SPEC_STATUS_SUCCESS
            ? formatMessage(MessageKey::COMPLETION_UNMET_AT_STEP, {step})
            : std::string(findMessage(MessageKey::COMPLETION_UNMET));
    return failure + listUnmetConditions(session);
}

std::string listUnmetConditions(const Session& session)
{
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(&session.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return std::string(); }
    std::string text {};
    std::optional<KiottySpecCondition> previous {};
    const std::unique_ptr<KiottySpecUnmetConditionPage> page_storage =
        std::make_unique<KiottySpecUnmetConditionPage>();
    KiottySpecUnmetConditionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchUnmetConditionPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecUnmetCondition& unmet = page.elements[index];
            text += describeUnmetCondition(unmet, previous);
            previous = unmet.condition;
        }
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    return text;
}

std::string describeUnmetCondition(const KiottySpecUnmetCondition& unmet,
                                   std::optional<KiottySpecCondition> previous)
{
    const bool is_new_condition = previous != unmet.condition;
    const std::string heading =
        is_new_condition ? "\n- " + describeCondition(unmet.condition) + ":" : std::string();
    return heading + " " + unmet.id;
}

std::string describeCondition(KiottySpecCondition condition)
{
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

}

std::vector<TagColumn> readTagDefinitions(const Session& session, KiottySpecItemKind kind)
{
    std::vector<TagColumn> tags {};
    const KiottySpecCursorResult cursor =
        KiottySpec_openTagDefinitionCursor(&session.context, kind);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return tags; }
    const std::unique_ptr<KiottySpecTagDefinitionPage> page_storage =
        std::make_unique<KiottySpecTagDefinitionPage>();
    KiottySpecTagDefinitionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchTagDefinitionPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            TagColumn& column = tags.emplace_back();
            column.index = page.elements[index].index;
            column.definition = page.elements[index];
        }
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    return tags;
}

std::string findRelationName(const Session& session, KiottySpecItemKind kind, uint32_t relation)
{
    const std::vector<KiottySpecRelationDefinition> relations =
        readRelationDefinitions(session, kind);
    for (const KiottySpecRelationDefinition& definition : relations) {
        if (definition.index == relation) { return definition.name; }
    }
    return "";
}

std::vector<KiottySpecRelationDefinition> readRelationDefinitions(const Session& session,
                                                                  KiottySpecItemKind kind)
{
    std::vector<KiottySpecRelationDefinition> relations {};
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationDefinitionCursor(&session.context, kind);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return relations; }
    const std::unique_ptr<KiottySpecRelationDefinitionPage> page_storage =
        std::make_unique<KiottySpecRelationDefinitionPage>();
    KiottySpecRelationDefinitionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationDefinitionPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            relations.push_back(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    return relations;
}

}
