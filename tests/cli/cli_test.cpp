#include "i18n/kiotty_guide_text.h"
#include "presentation/commands/kiotty_guide.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kiotty::cli;
using Check = std::function<bool()>;

constexpr KiottySpecCheckpointHandle LARGEST_CHECKPOINT_NUMBER = 999999999;
constexpr KiottySpecCheckpointHandle CURRENT_CHECKPOINT = KIOTTY_SPEC_CHECKPOINT_CURRENT;
const std::string INVALID_UTF8_SUFFIX = "\xff";

std::string appendNullByte(const std::string& text)
{
    std::string joined = text;
    joined.push_back('\0');
    return joined + "1";
}

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool testParseNumber()
{
    const std::optional<uint32_t> empty = parseNumber("");
    const std::optional<uint32_t> largest = parseNumber("4294967295");
    const std::optional<uint32_t> too_large = parseNumber("4294967296");
    const std::optional<uint32_t> with_null = parseNumber(appendNullByte("1"));
    const std::optional<uint32_t> with_bad_text = parseNumber("1" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty number is rejected") && passed;
    passed = expectTrue(largest == UINT32_MAX, "largest number is accepted") && passed;
    passed = expectTrue(!too_large, "number past the largest is rejected") && passed;
    passed = expectTrue(!with_null, "number with null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "number with bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseActor()
{
    const std::optional<KiottySpecActor> empty = parseActor("");
    const std::optional<KiottySpecActor> longest = parseActor("user");
    const std::optional<KiottySpecActor> too_long = parseActor("users");
    const std::optional<KiottySpecActor> with_null = parseActor(appendNullByte("user"));
    const std::optional<KiottySpecActor> with_bad_text = parseActor("ai" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(empty == KIOTTY_SPEC_ACTOR_AI, "empty actor is ai") && passed;
    passed = expectTrue(longest == KIOTTY_SPEC_ACTOR_USER, "user actor is accepted") && passed;
    passed = expectTrue(!too_long, "longer actor is rejected") && passed;
    passed = expectTrue(!with_null, "actor with null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "actor with bad UTF-8 is rejected") && passed;
    passed = expectTrue(parseActor("ai") == KIOTTY_SPEC_ACTOR_AI, "ai actor is accepted") && passed;
    return passed;
}

bool testParseCheckpoint()
{
    const KiottySpecCheckpointHandle seventh {7};
    const std::optional<KiottySpecCheckpointHandle> empty = parseCheckpoint("");
    const std::optional<KiottySpecCheckpointHandle> largest = parseCheckpoint("CP-999999999");
    const std::optional<KiottySpecCheckpointHandle> padded = parseCheckpoint("CP-0007");
    const std::optional<KiottySpecCheckpointHandle> too_long = parseCheckpoint("CP-1000000000");
    const std::optional<KiottySpecCheckpointHandle> with_null =
        parseCheckpoint(appendNullByte("CP-1"));
    const std::optional<KiottySpecCheckpointHandle> with_bad_text =
        parseCheckpoint("CP-1" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty checkpoint is rejected") && passed;
    passed = expectTrue(largest == LARGEST_CHECKPOINT_NUMBER, "nine digits are accepted") && passed;
    passed = expectTrue(padded == seventh, "padded checkpoint is accepted") && passed;
    passed = expectTrue(!too_long, "ten digits are rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseCheckpointArgument()
{
    const std::optional<KiottySpecCheckpointHandle> current = parseCheckpointArgument("current");
    const std::optional<KiottySpecCheckpointHandle> dash = parseCheckpointArgument("-");
    const std::optional<KiottySpecCheckpointHandle> largest =
        parseCheckpointArgument("CP-999999999");
    const std::optional<KiottySpecCheckpointHandle> too_long =
        parseCheckpointArgument("CP-1000000000");
    const std::optional<KiottySpecCheckpointHandle> with_null =
        parseCheckpointArgument(appendNullByte("current"));
    const std::optional<KiottySpecCheckpointHandle> with_bad_text =
        parseCheckpointArgument("current" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!parseCheckpointArgument(""), "empty argument is rejected") && passed;
    passed = expectTrue(current == CURRENT_CHECKPOINT, "current is accepted") && passed;
    passed = expectTrue(dash == CURRENT_CHECKPOINT, "dash is accepted") && passed;
    passed = expectTrue(largest == LARGEST_CHECKPOINT_NUMBER, "nine digits are accepted") && passed;
    passed = expectTrue(!too_long, "ten digits are rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseTagSpec()
{
    const std::string longest_name(KIOTTY_SPEC_MAX_TAG_NAME_LENGTH, 'a');
    const std::optional<TagSpecText> longest = parseTagSpec(longest_name + "=open");
    const std::optional<TagSpecText> too_long = parseTagSpec(longest_name + "a=open");
    const std::optional<TagSpecText> name_only = parseTagSpec("status");
    const std::optional<TagSpecText> with_null = parseTagSpec(appendNullByte("status") + "=open");
    const std::optional<TagSpecText> with_bad_text = parseTagSpec("st" + INVALID_UTF8_SUFFIX);
    const bool is_longest_kept = longest.has_value() && longest.value().name == longest_name &&
                                 longest.value().bits == std::string("open");
    const bool is_name_only = name_only.has_value() && !name_only.value().bits.has_value();
    const bool is_bad_text_kept =
        with_bad_text.has_value() && with_bad_text.value().name == "st" + INVALID_UTF8_SUFFIX;
    bool passed = true;
    passed = expectTrue(!parseTagSpec(""), "empty tag spec is rejected") && passed;
    passed = expectTrue(!parseTagSpec("=open"), "tag spec without a name is rejected") && passed;
    passed = expectTrue(is_longest_kept, "longest tag name is accepted") && passed;
    passed = expectTrue(!too_long, "tag name past the longest is rejected") && passed;
    passed = expectTrue(is_name_only, "tag name without bits is accepted") && passed;
    passed = expectTrue(!with_null, "tag name with null byte is rejected") && passed;
    passed = expectTrue(is_bad_text_kept, "bad UTF-8 name is left to the model lookup") && passed;
    return passed;
}

std::unique_ptr<KiottySpecTagDefinition> makeTagDefinition(const std::string& longest_name)
{
    std::unique_ptr<KiottySpecTagDefinition> definition =
        std::make_unique<KiottySpecTagDefinition>();
    std::memcpy(definition->name, "status", sizeof("status"));
    std::memcpy(definition->bit_names[0], longest_name.c_str(), longest_name.size() + 1);
    std::memcpy(definition->bit_names[1], "open", sizeof("open"));
    return definition;
}

bool testParseBits()
{
    const std::string longest_name(KIOTTY_SPEC_MAX_TAG_NAME_LENGTH, 'a');
    const std::unique_ptr<KiottySpecTagDefinition> storage = makeTagDefinition(longest_name);
    const KiottySpecTagDefinition& definition = *storage;
    const std::optional<uint64_t> longest = parseBits(definition, longest_name);
    const std::optional<uint64_t> both = parseBits(definition, longest_name + ",open");
    const std::optional<uint64_t> cleared = parseBits(definition, "-");
    const std::optional<uint64_t> empty = parseBits(definition, "");
    const std::optional<uint64_t> too_long = parseBits(definition, longest_name + "a");
    const std::optional<uint64_t> with_null = parseBits(definition, appendNullByte("open"));
    const std::optional<uint64_t> with_bad_text =
        parseBits(definition, "open" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty bits are rejected") && passed;
    passed = expectTrue(longest == uint64_t {1}, "longest bit name is accepted") && passed;
    passed = expectTrue(both == uint64_t {3}, "two bit names are combined") && passed;
    passed = expectTrue(cleared == uint64_t {0}, "dash clears the bits") && passed;
    passed = expectTrue(!too_long, "longer bit name is rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 bit name is rejected") && passed;
    return passed;
}

bool testParseArgumentsEmpty()
{
    const Arguments arguments = parseArguments({}, 0, {});
    const bool is_empty = arguments.positional.empty() && arguments.options.empty() &&
                          arguments.flags.empty();
    const Arguments blank = parseArguments({""}, 0, {});
    const bool is_blank_positional = blank.positional == std::vector<std::string> {""};
    bool passed = true;
    passed = expectTrue(is_empty, "no tokens give no arguments") && passed;
    passed = expectTrue(is_blank_positional, "empty token stays positional") && passed;
    return passed;
}

bool testParseArgumentsLength()
{
    const std::string longest(KIOTTY_SPEC_MAX_BODY_LENGTH, 'b');
    const std::string too_long = longest + "b";
    const Arguments arguments =
        parseArguments({"item", "--body", longest, "--summary", too_long}, 1, {});
    const bool is_longest_kept = arguments.option("body") == longest;
    const bool is_too_long_kept = arguments.option("summary") == too_long;
    bool passed = true;
    passed = expectTrue(is_longest_kept, "longest option value is kept") && passed;
    passed = expectTrue(is_too_long_kept, "longer option value is left to the library") && passed;
    return passed;
}

bool testParseArgumentsBytes()
{
    const std::string with_null = appendNullByte("title");
    const std::string with_bad_text = "title" + INVALID_UTF8_SUFFIX;
    const Arguments arguments =
        parseArguments({"--title", with_null, with_bad_text, "--tags"}, 0, {});
    const bool is_null_kept = arguments.option("title") == with_null;
    const bool is_bad_text_kept = arguments.positional == std::vector<std::string> {with_bad_text};
    const bool has_flag = arguments.flag("tags");
    bool passed = true;
    passed = expectTrue(is_null_kept, "null byte in an option value is kept") && passed;
    passed = expectTrue(is_bad_text_kept, "bad UTF-8 positional is left to the library") && passed;
    passed = expectTrue(has_flag, "known flag takes no value") && passed;
    return passed;
}

// UT-092: ks remote 명령의 Kind 인자는 rule 만 받는다.
bool testParseRemoteKind()
{
    const std::optional<KiottySpecItemKind> rule = parseRemoteKind("rule");
    bool passed = expectTrue(rule == KIOTTY_SPEC_ITEM_KIND_RULE, "rule is accepted");
    passed = expectTrue(!parseRemoteKind("private_rule"), "private_rule is rejected") && passed;
    passed = expectTrue(!parseRemoteKind("전용 개발 수칙"), "private rule model name is rejected") && passed;
    passed = expectTrue(!parseRemoteKind("requirement"), "other kind is rejected") && passed;
    passed = expectTrue(!parseRemoteKind("RULE"), "case differs") && passed;
    passed = expectTrue(!parseRemoteKind(""), "empty text is rejected") && passed;
    passed = expectTrue(!parseRemoteKind(appendNullByte("rule")), "null byte is rejected") && passed;
    passed = expectTrue(!parseRemoteKind("rules"), "longer than rule is rejected") && passed;
    passed = expectTrue(!parseRemoteKind("rule" + INVALID_UTF8_SUFFIX), "bad UTF-8 is rejected") && passed;
    return passed;
}

// UT-093: ks guide 의 주제 인자를 parse 한다.
bool testParseGuideTopic()
{
    const std::optional<GuideTopic> overview = parseGuideTopic({});
    const std::optional<GuideTopic> first = parseGuideTopic({"step", "1"});
    const std::optional<GuideTopic> last = parseGuideTopic({"step", "12"});
    const std::optional<GuideTopic> kind = parseGuideTopic({"kind", "전용 개발 수칙"});
    const std::optional<GuideTopic> remote = parseGuideTopic({"remote"});
    bool passed = expectTrue(overview && overview->subject == GuideSubject::OVERVIEW, "no topic");
    passed = expectTrue(first && first->subject == GuideSubject::STEP && first->step == 1,
                        "step 1") && passed;
    passed = expectTrue(last && last->step == KIOTTY_SPEC_STEP_COUNT, "step 12") && passed;
    passed = expectTrue(kind && kind->subject == GuideSubject::KIND &&
                            kind->kind == "전용 개발 수칙", "kind name is passed on") && passed;
    passed = expectTrue(remote && remote->subject == GuideSubject::REMOTE, "remote") && passed;
    passed = expectTrue(!parseGuideTopic({"step", "0"}), "step 0 is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"step", "13"}), "step 13 is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"step", "two"}), "step text is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"step"}), "step without number") && passed;
    passed = expectTrue(!parseGuideTopic({"kind", ""}), "empty kind is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"kind", appendNullByte("rule")}), "kind null byte") &&
             passed;
    passed = expectTrue(!parseGuideTopic({"other"}), "unknown topic is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({""}), "empty topic is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"remote", "rule"}), "remote takes nothing") && passed;
    passed = expectTrue(!parseGuideTopic({"step", "120"}), "step longer than 12 is rejected") && passed;
    passed = expectTrue(!parseGuideTopic({"step", "1" + INVALID_UTF8_SUFFIX}), "step bad UTF-8") && passed;
    passed = expectTrue(!parseGuideTopic({"remote" + INVALID_UTF8_SUFFIX}), "topic bad UTF-8") && passed;
    const std::optional<GuideTopic> bad_kind = parseGuideTopic({"kind", "rule" + INVALID_UTF8_SUFFIX});
    passed = expectTrue(bad_kind && bad_kind->kind == "rule" + INVALID_UTF8_SUFFIX,
                        "kind bad UTF-8 is left to kindArgument") && passed;
    return passed;
}

// UT-094: 12 단계와 Kind 마다 안내 문장이 있고, 사용자 몫 명령에 [user] 가 붙는다.
bool testGuideStepCoverage()
{
    bool passed = true;
    for (uint32_t step = 1; step <= KIOTTY_SPEC_STEP_COUNT; ++step) {
        const std::optional<StepGuide> guide = findStepGuide(step);
        const bool is_filled = guide && !guide->role.empty() && !guide->reading.empty() &&
                               !guide->linking.empty() && !guide->condition.empty() &&
                               !findShortStepName(step).empty();
        passed = expectTrue(is_filled, ("step " + std::to_string(step)).c_str()) && passed;
    }
    passed = expectTrue(!findStepGuide(0) && !findStepGuide(13), "steps out of range") && passed;
    for (uint32_t value = 1; value <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++value) {
        const std::optional<KindGuide> guide =
            findKindGuide(static_cast<KiottySpecItemKind>(value));
        const bool is_filled = guide && !guide->purpose.empty() && !guide->body.empty();
        passed = expectTrue(is_filled, ("kind " + std::to_string(value)).c_str()) && passed;
    }
    const std::string_view overview = findGuideOverview();
    const bool has_rules = overview.find("REQ, RULE, PRULE") != std::string_view::npos;
    passed = expectTrue(has_rules, "step 2 names the development rules") && passed;
    const std::string_view remote = findGuideRemote();
    for (const char* verb : {"remote accept", "remote discard", "remote push"}) {
        const size_t found = remote.find(verb);
        const bool is_user = found != std::string_view::npos && found >= 10 &&
                             remote.substr(found - 10, 7) == "[user] ";
        passed = expectTrue(is_user, verb) && passed;
    }
    return passed;
}

// UT-095: 진행 줄은 12 단계 이름을 잇고 지금 단계에만 (V) 를 붙인다.
bool testFormatStepProgress()
{
    const auto count_marks = [](const std::string& line) {
        size_t count = 0;
        for (size_t at = line.find("(V)"); at != std::string::npos; at = line.find("(V)", at + 1)) {
            ++count;
        }
        return count;
    };
    const std::string first = formatStepProgress(1);
    const std::string sixth = formatStepProgress(6);
    const std::string last = formatStepProgress(KIOTTY_SPEC_STEP_COUNT);
    const std::string none = formatStepProgress(0);
    bool passed = expectTrue(first.rfind("변경 요청 (V) → 요구사항", 0) == 0, "step 1 mark");
    passed = expectTrue(sixth.find("개발 계획 (V) → 구현") != std::string::npos, "step 6 mark") &&
             passed;
    const std::string last_mark = "E2E 실행 (V)";
    const bool is_last_marked = last.size() >= last_mark.size() &&
                                last.compare(last.size() - last_mark.size(), last_mark.size(),
                                             last_mark) == 0;
    passed = expectTrue(is_last_marked, "step 12 mark at end") && passed;
    passed = expectTrue(count_marks(first) == 1 && count_marks(sixth) == 1 &&
                            count_marks(last) == 1, "one mark") && passed;
    passed = expectTrue(count_marks(none) == 0, "no mark out of range") && passed;
    size_t arrows = 0;
    for (size_t at = none.find("→"); at != std::string::npos; at = none.find("→", at + 1)) {
        ++arrows;
    }
    passed = expectTrue(arrows == KIOTTY_SPEC_STEP_COUNT - 1, "eleven arrows") && passed;
    passed = expectTrue(none.rfind("변경 요청 → ", 0) == 0 &&
                            none.find("E2E 계획 → E2E 실행") != std::string::npos,
                        "names in order") && passed;
    return passed;
}

// UT-096: 확정 조건마다 reason · action · command 가 있고 ID 가 채워진다.
bool testDescribeUnmetCondition()
{
    bool passed = true;
    for (uint32_t value = KIOTTY_SPEC_CONDITION_ENTRY_STEP;
         value <= KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST; ++value) {
        const std::optional<ConditionAdvice> advice =
            findConditionAdvice(static_cast<KiottySpecCondition>(value), "X-001");
        bool is_filled = advice && !advice->reason.empty() && !advice->actions.empty();
        bool has_id = false;
        if (advice) {
            for (const NextAction& action : advice->actions) {
                is_filled = is_filled && !action.action.empty() && !action.command.empty();
                is_filled = is_filled && action.command.find('{') == std::string::npos;
                has_id = has_id || action.command.find("X-001") != std::string::npos;
            }
        }
        const std::string label = "condition " + std::to_string(value);
        passed = expectTrue(is_filled && has_id, label.c_str()) && passed;
    }
    const std::optional<ConditionAdvice> rule =
        findConditionAdvice(KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR, "RULE-001");
    const std::optional<ConditionAdvice> private_rule =
        findConditionAdvice(KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR, "PRULE-001");
    passed = expectTrue(rule && rule->actions.back().command.find("\"검사 개발 수칙\" RULE-001") !=
                                    std::string::npos, "rule relation") && passed;
    passed = expectTrue(private_rule && private_rule->actions.back().command.find(
                            "\"검사 전용 개발 수칙\" PRULE-001") != std::string::npos,
                        "private rule relation") && passed;
    passed = expectTrue(!findConditionAdvice(static_cast<KiottySpecCondition>(0), "X-001"),
                        "unknown condition fails") && passed;
    passed = expectTrue(!findConditionAdvice(static_cast<KiottySpecCondition>(KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST + 1), "X-001"),
                        "condition past the last fails") && passed;
    return passed;
}

NextState makeNextState(uint32_t step, KiottySpecCheckpointState state,
                        std::vector<KiottySpecUnmetCondition> unmet)
{
    NextState next {};
    next.has_database = true;
    next.position.current = step == 0 ? 0 : 1;
    next.position.step = static_cast<KiottySpecStep>(step);
    next.position.state = state;
    next.unmet = std::move(unmet);
    return next;
}

KiottySpecUnmetCondition makeUnmet(KiottySpecCondition condition, const char* id)
{
    KiottySpecUnmetCondition unmet {};
    std::snprintf(unmet.id, sizeof(unmet.id), "%s", id);
    unmet.condition = condition;
    return unmet;
}

bool expectCommands(const std::vector<NextAction>& actions,
                    const std::vector<std::string>& commands, const char* label)
{
    bool is_same = actions.size() == commands.size();
    for (size_t index = 0; is_same && index < commands.size(); ++index) {
        is_same = actions[index].command == commands[index];
    }
    if (!is_same) {
        for (const NextAction& action : actions) { std::printf("    got: %s\n", action.command.c_str()); }
    }
    return expectTrue(is_same, label);
}

// UT-097: 위치와 어긋난 확정 조건으로 할 행동을 고른다.
bool testChooseNextActions()
{
    constexpr KiottySpecCheckpointState RUNNING = KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING;
    constexpr KiottySpecCheckpointState DONE = KIOTTY_SPEC_CHECKPOINT_STATE_DONE;
    const std::string start = "ks checkpoint start ";
    const std::string described = " --description \"...\"";
    bool passed = expectCommands(chooseNextActions(NextState {}), {"ks init --name \"...\""},
                                 "no database");
    passed = expectCommands(chooseNextActions(makeNextState(0, RUNNING, {})),
                            {start + "1" + described, "ks guide step 1"}, "no checkpoint") &&
             passed;
    const KiottySpecCondition pair = KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR;
    passed = expectCommands(
                 chooseNextActions(makeNextState(8, RUNNING,
                                                 {makeUnmet(pair, "REQ-003"), makeUnmet(pair, "REQ-004"),
                                                  makeUnmet(pair, "REQ-005")})),
                 {"ks item add system_test_plan --title \"...\"",
                  "ks relation add ST-xxx \"검증 요구사항\" REQ-003"},
                 "one condition, three items") && passed;
    passed = expectCommands(
                 chooseNextActions(makeNextState(
                     12, RUNNING,
                     {makeUnmet(KIOTTY_SPEC_CONDITION_PASSING_RESULT, "E2ER-001"),
                      makeUnmet(KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST, "CR-016")})),
                 {"ks tag set E2ER-001 결과 PASS", "ks tag set CR-016 상태 완료"},
                 "two conditions in order") && passed;
    passed = expectCommands(chooseNextActions(makeNextState(3, RUNNING, {})),
                            {"[user] ks checkpoint complete --actor user"}, "all met") && passed;
    passed = expectCommands(chooseNextActions(makeNextState(6, DONE, {})),
                            {start + "7" + described, "ks guide step 7"}, "step 6 done") && passed;
    NextState idle = makeNextState(12, DONE, {});
    passed = expectCommands(chooseNextActions(idle), {"-"}, "nothing to do") && passed;
    idle.position.open_change_requests = 1;
    passed = expectCommands(chooseNextActions(idle), {start + "1" + described, "ks guide step 1"},
                            "open change request after step 12") && passed;
    return passed;
}

}

int main()
{
    const std::pair<const char*, Check> tests[] = {
        {"parse number", testParseNumber},
        {"parse actor", testParseActor},
        {"parse checkpoint", testParseCheckpoint},
        {"parse checkpoint argument", testParseCheckpointArgument},
        {"parse tag spec", testParseTagSpec},
        {"parse bits", testParseBits},
        {"parse arguments empty", testParseArgumentsEmpty},
        {"parse arguments length", testParseArgumentsLength},
        {"parse arguments bytes", testParseArgumentsBytes},
        {"parse remote kind", testParseRemoteKind},
        {"parse guide topic", testParseGuideTopic},
        {"guide step coverage", testGuideStepCoverage},
        {"format step progress", testFormatStepProgress},
        {"describe unmet condition", testDescribeUnmetCondition},
        {"choose next actions", testChooseNextActions},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
