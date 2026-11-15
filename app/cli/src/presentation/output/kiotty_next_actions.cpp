#include "presentation/commands/kiotty_guide.h"

#include "i18n/kiotty_guide_text.h"

#include <algorithm>

namespace kiotty::cli {

namespace {

using Key = MessageKey;

constexpr std::string_view PROGRESS_SEPARATOR = " → ";
constexpr std::string_view CURRENT_MARK = " (V)";
constexpr std::string_view ID_SLOT = "{0}";
constexpr std::string_view RELATION_SLOT = "{1}";
constexpr std::string_view PRIVATE_RULE_PREFIX = "PRULE";
constexpr std::string_view INIT_COMMAND = "ks init --name \"...\"";
constexpr std::string_view COMPLETE_COMMAND = "[user] ks checkpoint complete --actor user";
constexpr std::string_view IDLE_COMMAND = "-";
constexpr size_t TOPIC_PAIR_LENGTH = 2;
constexpr uint32_t FIRST_STEP = 1;

std::optional<GuideTopic> parseTopicPair(const std::string& subject, const std::string& value);
std::vector<NextAction> chooseRunningActions(const std::vector<KiottySpecUnmetCondition>& unmet);
std::vector<NextAction> findAdvisedActions(const KiottySpecUnmetCondition& unmet);
std::vector<NextAction> mergeActions(std::vector<NextAction> actions,
                                     const std::vector<NextAction>& added);
bool hasAction(const std::vector<NextAction>& actions, const NextAction& wanted);
std::vector<NextAction> makeStartActions(uint32_t step);
NextAction makeAction(MessageKey key, std::string_view command);
std::optional<ConditionAdvice> makeConditionAdvice(KiottySpecCondition condition,
                                                   std::string_view id);
std::string fillCommand(std::string_view pattern, std::string_view id);
std::string replaceSlot(std::string text, std::string_view slot, std::string_view value);
std::string cutIdPrefix(std::string_view id);

}

std::optional<GuideTopic> parseGuideTopic(const std::vector<std::string>& words)
{
    const size_t count = words.size();
    if (count == 0) { return GuideTopic {}; }
    const std::string& subject = words.front();
    const bool is_remote = count == 1 && subject == "remote";
    if (is_remote) {
        GuideTopic topic {};
        topic.subject = GuideSubject::REMOTE;
        return topic;
    }
    const bool is_pair = count == TOPIC_PAIR_LENGTH;
    if (!is_pair) { return std::nullopt; }
    return parseTopicPair(subject, words.back());
}

std::string formatStepProgress(uint32_t step)
{
    std::string line {};
    for (uint32_t index = FIRST_STEP; index <= KIOTTY_SPEC_STEP_COUNT; ++index) {
        const bool is_first = index == FIRST_STEP;
        const bool is_current = index == step;
        line += is_first ? std::string_view() : PROGRESS_SEPARATOR;
        line += findShortStepName(index);
        line += is_current ? CURRENT_MARK : std::string_view();
    }
    return line;
}

std::optional<ConditionAdvice> findConditionAdvice(KiottySpecCondition condition,
                                                   std::string_view id)
{ return makeConditionAdvice(condition, id); }

std::vector<NextAction> chooseNextActions(const NextState& state)
{
    const bool has_database = state.has_database;
    if (!has_database) { return {makeAction(Key::NEXT_INIT_ACTION, INIT_COMMAND)}; }
    const KiottySpecPosition& position = state.position;
    const bool has_checkpoint = position.current != 0;
    if (!has_checkpoint) { return makeStartActions(FIRST_STEP); }
    const bool is_done = position.state == KIOTTY_SPEC_CHECKPOINT_STATE_DONE;
    if (!is_done) { return chooseRunningActions(state.unmet); }
    const bool is_last = position.step >= KIOTTY_SPEC_STEP_COUNT;
    const uint32_t next_step = static_cast<uint32_t>(position.step) + FIRST_STEP;
    if (!is_last) { return makeStartActions(next_step); }
    const bool has_open_request = position.open_change_requests > 0;
    if (has_open_request) { return makeStartActions(FIRST_STEP); }
    return {makeAction(Key::NEXT_IDLE_ACTION, IDLE_COMMAND)};
}

std::string findIdPrefix(std::string_view id) { return cutIdPrefix(id); }

namespace {

std::optional<GuideTopic> parseTopicPair(const std::string& subject, const std::string& value)
{
    GuideTopic topic {};
    const bool has_null = value.find('\0') != std::string::npos;
    const bool is_kind = subject == "kind" && value.size() > 0 && !has_null;
    if (is_kind) {
        topic.subject = GuideSubject::KIND;
        topic.kind = value;
        return topic;
    }
    const bool is_step_subject = subject == "step";
    if (!is_step_subject) { return std::nullopt; }
    const std::optional<uint32_t> step = parseNumber(value);
    const uint32_t number = step.value_or(0);
    const bool is_step = number >= FIRST_STEP && number <= KIOTTY_SPEC_STEP_COUNT;
    if (!is_step) { return std::nullopt; }
    topic.subject = GuideSubject::STEP;
    topic.step = number;
    return topic;
}

std::vector<NextAction> chooseRunningActions(const std::vector<KiottySpecUnmetCondition>& unmet)
{
    const bool is_met = unmet.empty();
    if (is_met) { return {makeAction(Key::NEXT_COMPLETE_ACTION, COMPLETE_COMMAND)}; }
    std::vector<NextAction> actions {};
    std::vector<std::string> seen {};
    for (const KiottySpecUnmetCondition& condition : unmet) {
        const std::string key =
            std::to_string(condition.condition) + ":" + cutIdPrefix(condition.id);
        const bool is_seen = std::find(seen.begin(), seen.end(), key) != seen.end();
        if (is_seen) { continue; }
        seen.push_back(key);
        actions = mergeActions(std::move(actions), findAdvisedActions(condition));
    }
    return actions;
}

std::vector<NextAction> findAdvisedActions(const KiottySpecUnmetCondition& unmet)
{
    const std::optional<ConditionAdvice> advice = makeConditionAdvice(unmet.condition, unmet.id);
    const ConditionAdvice found = advice.value_or(ConditionAdvice {});
    return found.actions;
}

std::vector<NextAction> mergeActions(std::vector<NextAction> actions,
                                     const std::vector<NextAction>& added)
{
    for (const NextAction& action : added) {
        const bool has_action = hasAction(actions, action);
        if (!has_action) { actions.push_back(action); }
    }
    return actions;
}

bool hasAction(const std::vector<NextAction>& actions, const NextAction& wanted)
{
    for (const NextAction& action : actions) {
        const bool is_same = action.action == wanted.action && action.command == wanted.command;
        if (is_same) { return true; }
    }
    return false;
}

std::vector<NextAction> makeStartActions(uint32_t step)
{
    const std::string number = std::to_string(step);
    NextAction start = makeAction(Key::NEXT_START_ACTION, IDLE_COMMAND);
    start.command = "ks checkpoint start " + number + " --description \"...\"";
    NextAction read = makeAction(Key::NEXT_READ_ACTION, IDLE_COMMAND);
    read.command = "ks guide step " + number;
    return {start, read};
}

NextAction makeAction(MessageKey key, std::string_view command)
{ return NextAction {std::string(findMessage(key)), std::string(command)}; }

std::optional<ConditionAdvice> makeConditionAdvice(KiottySpecCondition condition,
                                                   std::string_view id)
{
    const std::optional<ConditionText> found = findConditionText(static_cast<uint32_t>(condition));
    if (!found) { return std::nullopt; }
    const ConditionText& text = found.value();
    ConditionAdvice advice {};
    advice.reason = std::string(text.reason);
    advice.actions.push_back(
        NextAction {std::string(text.first_action), fillCommand(text.first_command, id)});
    const bool has_second = text.second_action.size() > 0;
    if (has_second) {
        advice.actions.push_back(
            NextAction {std::string(text.second_action), fillCommand(text.second_command, id)});
    }
    return advice;
}

std::string fillCommand(std::string_view pattern, std::string_view id)
{
    const bool is_private = cutIdPrefix(id) == PRIVATE_RULE_PREFIX;
    const MessageKey relation =
        is_private ? Key::NEXT_INSPECTED_PRIVATE_RULE : Key::NEXT_INSPECTED_RULE;
    const std::string filled = replaceSlot(std::string(pattern), ID_SLOT, id);
    return replaceSlot(filled, RELATION_SLOT, findMessage(relation));
}

std::string replaceSlot(std::string text, std::string_view slot, std::string_view value)
{
    const size_t position = text.find(slot);
    const bool has_slot = position != std::string::npos;
    if (has_slot) { text.replace(position, slot.size(), value); }
    return text;
}

std::string cutIdPrefix(std::string_view id)
{
    const size_t dash = id.find('-');
    return std::string(id.substr(0, dash));
}

}

}
