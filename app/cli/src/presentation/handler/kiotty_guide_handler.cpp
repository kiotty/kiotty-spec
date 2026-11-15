#include "presentation/commands/kiotty_guide.h"

#include "i18n/kiotty_guide_text.h"

#include <iostream>
#include <memory>

namespace kiotty::cli {

namespace {

using Key = MessageKey;

constexpr std::string_view LABEL_INDENT = "  ";

int printStepGuide(const Session& session, uint32_t step);
int printKindTopic(const Session& session, const std::string& text);
void printKindGuide(const Session& session, const KiottySpecModel& model);
std::string describeTagValues(const KiottySpecTagDefinition& definition);
void printPosition(const KiottySpecPosition& position);
std::optional<std::vector<KiottySpecUnmetCondition>> readUnmetConditions(const Session& session);
void printUnmetConditions(const Session& session,
                          const std::vector<KiottySpecUnmetCondition>& unmet);
std::string findKindOfId(const Session& session, std::string_view id);
void printActions(const std::vector<NextAction>& actions);

}

int showGuide(const Session& session)
{
    const std::optional<GuideTopic> topic = parseGuideTopic(session.arguments.positional);
    if (!topic) { return usage(findMessage(Key::GUIDE_TOPIC_USAGE)); }
    const GuideTopic& chosen = topic.value();
    switch (chosen.subject) {
    case GuideSubject::OVERVIEW: std::cout << findGuideOverview(); return 0;
    case GuideSubject::REMOTE: std::cout << findGuideRemote(); return 0;
    case GuideSubject::STEP: break;
    case GuideSubject::KIND: break;
    }
    const KiottySpecStatus opened = openDatabase(session);
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, opened); }
    const bool is_step = chosen.subject == GuideSubject::STEP;
    if (is_step) { return printStepGuide(session, chosen.step); }
    return printKindTopic(session, chosen.kind);
}

int showNext(const Session& session)
{
    NextState state {};
    state.has_database = hasDatabase(session);
    const bool has_database = state.has_database;
    if (!has_database) {
        printActions(chooseNextActions(state));
        return 0;
    }
    const KiottySpecStatus opened = openDatabase(session);
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, opened); }
    const KiottySpecPositionResult position = KiottySpec_readPosition(&session.context);
    if (position.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, position.code); }
    state.position = position.value;
    const bool has_checkpoint = position.value.current != 0;
    if (has_checkpoint) { printPosition(position.value); }
    const bool is_running =
        has_checkpoint && position.value.state != KIOTTY_SPEC_CHECKPOINT_STATE_DONE;
    if (is_running) {
        const std::optional<std::vector<KiottySpecUnmetCondition>> unmet =
            readUnmetConditions(session);
        if (!unmet) { return 1; }
        state.unmet = unmet.value();
        printUnmetConditions(session, state.unmet);
    }
    printActions(chooseNextActions(state));
    return 0;
}

namespace {

int printStepGuide(const Session& session, uint32_t step)
{
    const std::optional<StepGuide> found = findStepGuide(step);
    if (!found) { return usage(findMessage(Key::GUIDE_TOPIC_USAGE)); }
    const StepGuide& guide = found.value();
    std::cout << "Step " << stepName(step) << " — " << guide.role << "\n"
              << findMessage(Key::GUIDE_READING) << guide.reading << "\n\n";
    for (uint32_t value = 1; value <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++value) {
        const KiottySpecModelResult model =
            KiottySpec_readModel(&session.context, static_cast<KiottySpecItemKind>(value));
        const bool is_in_step =
            model.code == KIOTTY_SPEC_STATUS_SUCCESS &&
            static_cast<uint32_t>(model.value.created_in) == step;
        if (is_in_step) { printKindGuide(session, model.value); }
    }
    std::cout << "\n"
              << findMessage(Key::GUIDE_LINKING) << guide.linking << "\n"
              << findMessage(Key::GUIDE_CONDITION) << guide.condition << "\n";
    return 0;
}

int printKindTopic(const Session& session, const std::string& text)
{
    const std::optional<KiottySpecItemKind> kind = kindArgument(session, text);
    if (!kind) { return USAGE_EXIT_CODE; }
    const KiottySpecModelResult model = KiottySpec_readModel(&session.context, *kind);
    if (model.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, model.code); }
    printKindGuide(session, model.value);
    std::cout << LABEL_INDENT << findMessage(Key::GUIDE_STEP) << stepName(model.value.created_in)
              << " (ks guide step " << model.value.created_in << ")\n"
              << LABEL_INDENT << findMessage(Key::GUIDE_DECISION) << "ks decision show "
              << kindName(*kind) << "\n";
    return 0;
}

void printKindGuide(const Session& session, const KiottySpecModel& model)
{
    const std::optional<KindGuide> guide = findKindGuide(static_cast<uint32_t>(model.kind));
    const KindGuide text = guide.value_or(KindGuide {});
    std::cout << kindName(model.kind) << " (" << model.id_prefix << ")  " << text.purpose << "\n"
              << LABEL_INDENT << findMessage(Key::GUIDE_BODY) << text.body << "\n";
    const std::vector<TagColumn> tags = readTagDefinitions(session, model.kind);
    for (const TagColumn& tag : tags) {
        std::cout << LABEL_INDENT << findMessage(Key::GUIDE_TAG) << tag.definition.name << " = "
                  << describeTagValues(tag.definition) << "\n";
    }
    const std::vector<KiottySpecRelationDefinition> relations =
        readRelationDefinitions(session, model.kind);
    for (const KiottySpecRelationDefinition& relation : relations) {
        std::cout << LABEL_INDENT << findMessage(Key::GUIDE_RELATION) << relation.name << " → "
                  << kindName(relation.target_kind) << "\n";
    }
}

std::string describeTagValues(const KiottySpecTagDefinition& definition)
{
    uint64_t named = 0;
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const bool is_named = definition.bit_names[bit][0] != 0;
        if (is_named) { named |= uint64_t {1} << bit; }
    }
    return bitNames(definition, named);
}

void printPosition(const KiottySpecPosition& position)
{
    const bool is_done = position.state == KIOTTY_SPEC_CHECKPOINT_STATE_DONE;
    const MessageKey state = is_done ? Key::NEXT_DONE : Key::NEXT_RUNNING;
    std::cout << checkpointName(position.current) << "  " << stepName(position.step) << "  "
              << findMessage(state) << "\n"
              << formatStepProgress(position.step) << "\n\n";
}

std::optional<std::vector<KiottySpecUnmetCondition>> readUnmetConditions(const Session& session)
{
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(&session.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, cursor.code);
        return std::nullopt;
    }
    std::vector<KiottySpecUnmetCondition> unmet {};
    const std::unique_ptr<KiottySpecUnmetConditionPage> page_storage =
        std::make_unique<KiottySpecUnmetConditionPage>();
    KiottySpecUnmetConditionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchUnmetConditionPage(&session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        unmet.insert(unmet.end(), page.elements, page.elements + page.length);
    }
    KiottySpec_closeCursor(&session.context, cursor.value);
    return unmet;
}

void printUnmetConditions(const Session& session,
                          const std::vector<KiottySpecUnmetCondition>& unmet)
{
    const bool is_met = unmet.empty();
    if (is_met) { return; }
    printRow({"id", "kind", "reason"});
    for (const KiottySpecUnmetCondition& condition : unmet) {
        const std::optional<ConditionAdvice> advice =
            findConditionAdvice(condition.condition, condition.id);
        const std::string reason = advice.value_or(ConditionAdvice {}).reason;
        printRow({condition.id, findKindOfId(session, condition.id), reason});
    }
    std::cout << "\n";
}

std::string findKindOfId(const Session& session, std::string_view id)
{
    const std::string prefix = findIdPrefix(id);
    for (uint32_t value = 1; value <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++value) {
        const KiottySpecItemKind kind = static_cast<KiottySpecItemKind>(value);
        const KiottySpecModelResult model = KiottySpec_readModel(&session.context, kind);
        const bool is_match =
            model.code == KIOTTY_SPEC_STATUS_SUCCESS && prefix == model.value.id_prefix;
        if (is_match) { return kindName(kind); }
    }
    return "-";
}

void printActions(const std::vector<NextAction>& actions)
{
    printRow({"action", "command"});
    for (const NextAction& action : actions) { printRow({action.action, action.command}); }
}

}

}
