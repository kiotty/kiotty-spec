// CR-018: HistoryFinder 는 Checker 를 들지 않고 Checker 결과를 인자로 받는다.

#include "kiotty_spec.h"
#include "implementation/context/kiotty_context.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace {

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;

constexpr size_t PAGE_ROWS = 8;
constexpr uint64_t OPEN_REQUESTS = 2;

enum class Completion { OPEN, DONE };

constexpr KiottySpecStep NO_STEP = static_cast<KiottySpecStep>(0);

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

KiottySpecText textOf(const char* text) { return KiottySpecText{text, std::strlen(text)}; }

std::string databaseOf(const char* name)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error) / name;
    return (root / ".kiotty" / "spec.kisp").generic_string();
}

void removeProject(const char* name)
{
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::temp_directory_path(error) / name, error);
}

void prepareProject(const char* name, KiottySpecStep step, Completion completion)
{
    removeProject(name);
    KiottySpecConfiguration configuration {};
    KiottySpecContext* context = KiottySpec_createContext(&configuration).value;
    KiottySpec_open(context, textOf(databaseOf(name).c_str()));
    const bool has_checkpoint = step != NO_STEP;
    if (has_checkpoint) { KiottySpec_startCheckpoint(context, step, textOf("준비")); }
    if (completion == Completion::DONE) { KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER); }
    KiottySpec_releaseContext(context);
}

std::unique_ptr<impl::Context> openInternal(const char* name)
{
    auto context = std::make_unique<impl::Context>(impl::Configuration {});
    const std::string database = databaseOf(name);
    context->open(impl::Span<const char>(database.c_str(), database.size()));
    return context;
}

entity::UnmetCondition unmetOf(const char (&id)[7], KiottySpecCondition condition)
{
    entity::UnmetCondition unmet {};
    unmet.id.assign(impl::literalText(id));
    unmet.condition = condition;
    return unmet;
}

bool hasRow(const impl::CursorRow& row, const char* id, KiottySpecCondition condition)
{
    const entity::IdText read = impl::unpackId(row.first, row.second);
    const bool is_same_id = std::strcmp(read.view().data(), id) == 0;
    return is_same_id && row.third == static_cast<uint64_t>(condition);
}

bool testLatestStep()
{
    prepareProject("kiotty_query_latest", NO_STEP, Completion::OPEN);
    bool passed = true;
    {
        const std::unique_ptr<impl::Context> context = openInternal("kiotty_query_latest");
        impl::HistoryFinder history = (*context->opened().value).readSide().finder().history();
        passed = expectTrue(history.latestStep().code == KIOTTY_SPEC_STATUS_NOT_FOUND, "no checkpoint is not found");
    }
    prepareProject("kiotty_query_latest", KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, Completion::OPEN);
    {
        const std::unique_ptr<impl::Context> context = openInternal("kiotty_query_latest");
        impl::HistoryFinder history = (*context->opened().value).readSide().finder().history();
        const auto step = history.latestStep();
        passed = expectTrue(step.code == KIOTTY_SPEC_STATUS_SUCCESS && step.value == entity::Step::CHANGE_REQUEST_REPORTED,
                            "latest step is change request") && passed;
    }
    removeProject("kiotty_query_latest");
    return passed;
}

bool testUnmetRowsFromArguments()
{
    prepareProject("kiotty_query_unmet", NO_STEP, Completion::OPEN);
    const std::unique_ptr<impl::Context> context = openInternal("kiotty_query_unmet");
    const impl::ContextReadSide read = (*context->opened().value).readSide();
    impl::HistoryFinder history = read.finder().history();
    const auto empty = history.listUnmet(impl::Span<const entity::UnmetCondition>());
    bool passed = expectTrue(empty.code == KIOTTY_SPEC_STATUS_SUCCESS && read.cursors().total(empty.value).value == 0,
                             "empty list gives empty cursor");
    read.cursors().close(empty.value);
    const entity::UnmetCondition unmet[] = {unmetOf("CR-001", KIOTTY_SPEC_CONDITION_ENTRY_STEP),
                                            unmetOf("UT-002", KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR)};
    const auto cursor = history.listUnmet(impl::Span<const entity::UnmetCondition>(unmet, 2));
    const auto rows = read.cursors().fetch(cursor.value, impl::CursorKind::UNMET_CONDITION, PAGE_ROWS);
    passed = expectTrue(rows.code == KIOTTY_SPEC_STATUS_SUCCESS && rows.value.length() == 2, "two rows in given order") && passed;
    const bool is_kept = rows.value.length() == 2 && hasRow(rows.value[0], "CR-001", KIOTTY_SPEC_CONDITION_ENTRY_STEP) &&
                         hasRow(rows.value[1], "UT-002", KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR);
    passed = expectTrue(is_kept, "row keeps id and condition") && passed;
    const auto wrong = read.cursors().fetch(cursor.value, impl::CursorKind::ITEM, PAGE_ROWS);
    passed = expectTrue(wrong.code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "unmet cursor kind") && passed;
    read.cursors().close(cursor.value);
    removeProject("kiotty_query_unmet");
    return passed;
}

bool testPositionFromSummary()
{
    prepareProject("kiotty_query_position", KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, Completion::DONE);
    const std::unique_ptr<impl::Context> context = openInternal("kiotty_query_position");
    impl::HistoryFinder history = (*context->opened().value).readSide().finder().history();
    const auto entered = history.locate(entity::ChangeRequestSummary {OPEN_REQUESTS, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING});
    bool passed = expectTrue(entered.value.open_change_requests == OPEN_REQUESTS, "open change requests from summary");
    passed = expectTrue(entered.value.next_step == KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, "entry step 4 gives next step 4") && passed;
    const auto plain = history.locate(entity::ChangeRequestSummary {});
    passed = expectTrue(plain.value.next_step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, "no entry step gives requirement step") && passed;
    removeProject("kiotty_query_position");
    return passed;
}

}

int main()
{
    const std::pair<const char*, bool (*)()> tests[] = {
        {"latest step", testLatestStep},
        {"unmet rows from arguments", testUnmetRowsFromArguments},
        {"position from summary", testPositionFromSummary},
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
