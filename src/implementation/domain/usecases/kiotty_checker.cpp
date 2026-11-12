#include "implementation/domain/usecases/kiotty_checker.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/checker/kiotty_acceptance_criteria.h"

#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::RelationRecord;

using IndexResult = Result<uint32_t, KiottySpecStatus>;

constexpr uint32_t MISSING = 0xFFFFFFFFu;
constexpr ItemKind RESULT_KINDS[] = { ItemKind::SYSTEM_TEST_RESULT,
                                      ItemKind::END_TO_END_TEST_RESULT,
                                      ItemKind::CODE_INSPECTION_RESULT };

uint32_t findLowestStep(uint64_t value);
bool isResultKind(ItemKind kind);
size_t appendCondition(Span<UnmetCondition> rows, size_t length, const UnmetCondition& row);

}

Span<const UnmetCondition> Checker::collect(entity::Step step)
{
    _length = 0;
    switch (step) {
    case entity::Step::CHANGE_REQUEST_REPORTED: checkEntrySteps(); break;
    case entity::Step::REQUIREMENT_DISCUSSING: checkAcceptanceCriteria(); break;
    case entity::Step::DEVELOPMENT_PLAN_DISCUSSING: checkMilestoneCoverage(); break;
    case entity::Step::SYSTEM_TEST_PLAN_DISCUSSING:
        checkOneToOne(PairRule { SYSTEM_TEST_REQUIREMENT_RELATION, ItemKind::REQUIREMENT,
                                 ItemKind::SYSTEM_TEST_PLAN, KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR,
                                 TargetFilter::FUNCTIONAL_REQUIREMENTS });
        break;
    case entity::Step::CODE_INSPECTING:
        checkOneToOne(PairRule { CODE_INSPECTION_RULE_RELATION, ItemKind::RULE,
                                 ItemKind::CODE_INSPECTION_RESULT,
                                 KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR,
                                 TargetFilter::ALL_TARGETS });
        break;
    case entity::Step::END_TO_END_TEST_DISCUSSING:
        checkOneToOne(PairRule { END_TO_END_TEST_INTERFACE_RELATION, ItemKind::EXTERNAL_INTERFACE,
                                 ItemKind::END_TO_END_TEST_PLAN,
                                 KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR,
                                 TargetFilter::ALL_TARGETS });
        break;
    case entity::Step::END_TO_END_TEST_RUN: checkResults(); break;
    case entity::Step::EXTERNAL_INTERFACE_DISCUSSING:
    case entity::Step::DESIGN_DISCUSSING:
    case entity::Step::UNIT_TEST_PLAN_DISCUSSING:
    case entity::Step::IMPLEMENTING:
    case entity::Step::SYSTEM_TEST_RUN: break;
    }
    return viewVector(_rows).subspan(0, _length).value;
}

size_t Checker::openChangeRequestCount()
{
    size_t count = 0;
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_change_request = item && (*item.value).kind == ItemKind::CHANGE_REQUEST;
        if (!is_change_request) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (is_open) { ++count; }
    }
    return count;
}

uint32_t Checker::earliestEntryStep()
{
    const IndexResult entry = _workspace.catalog.finder().findTag(
        ItemKind::CHANGE_REQUEST, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    if (!entry) { return 0; }
    uint32_t earliest = 0;
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_change_request = item && (*item.value).kind == ItemKind::CHANGE_REQUEST;
        if (!is_change_request) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (!is_open) { continue; }
        const uint64_t value = _workspace.items.tags().value(handle, entry.value);
        const uint32_t step = findLowestStep(value);
        if (step != 0 && (earliest == 0 || step < earliest)) { earliest = step; }
    }
    return earliest;
}

void Checker::checkEntrySteps()
{
    const IndexResult entry = _workspace.catalog.finder().findTag(
        ItemKind::CHANGE_REQUEST, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_change_request = item && (*item.value).kind == ItemKind::CHANGE_REQUEST;
        if (!is_change_request) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (!is_open) { continue; }
        const uint64_t entry_step = entry ? _workspace.items.tags().value(handle, entry.value) : 0;
        if (entry_step != 0) { continue; }
        add(KIOTTY_SPEC_CONDITION_ENTRY_STEP, (*item.value).id);
    }
}

void Checker::checkAcceptanceCriteria()
{
    const TagBit functional =
        tagBit(ItemKind::REQUIREMENT, REQUIREMENT_KIND_TAG, FUNCTIONAL_REQUIREMENT_BIT);
    if (functional.tag_index == MISSING) { return; }
    std::vector<char>& buffer = _body;
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_requirement = item && (*item.value).kind == ItemKind::REQUIREMENT;
        if (!is_requirement) { continue; }
        const uint64_t kind_value = _workspace.items.tags().value(handle, functional.tag_index);
        if ((kind_value & functional.bit) == 0) { continue; }
        const Span<char> body = viewVector(buffer);
        const Result<size_t, KiottySpecStatus> length =
            _workspace.items.reader().readBody(handle, body);
        const auto text = body.subspan(0, length.value);
        const size_t criteria_count = length ? countAcceptanceCriteria(text.value) : 0;
        if (criteria_count != 0) { continue; }
        add(KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA, (*item.value).id);
    }
}

void Checker::checkMilestoneCoverage()
{
    const uint32_t designs = relationIndex(ItemKind::MILESTONE, MILESTONE_DESIGN_RELATION);
    const uint32_t tests = relationIndex(ItemKind::MILESTONE, MILESTONE_UNIT_TEST_RELATION);
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        if (!item) { continue; }
        uint32_t relation = MISSING;
        if ((*item.value).kind == ItemKind::DESIGN) { relation = designs; }
        if ((*item.value).kind == ItemKind::UNIT_TEST_PLAN) { relation = tests; }
        if (relation == MISSING) { continue; }
        const size_t count =
            countRelations(ItemKind::MILESTONE, relation, handle, RelationSide::TO);
        if (count == 0) { add(KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE, (*item.value).id); }
    }
}

void Checker::checkOneToOne(const PairRule& rule)
{
    const uint32_t relation = relationIndex(rule.test_kind, rule.relation_name);
    checkTargetPairs(rule, relation);
    checkSingleTargets(rule.test_kind, relation);
}

void Checker::checkTargetPairs(const PairRule& rule, uint32_t relation)
{
    const bool is_functional_only = rule.filter == TargetFilter::FUNCTIONAL_REQUIREMENTS;
    const TagBit functional = is_functional_only ? tagBit(ItemKind::REQUIREMENT,
                                                          REQUIREMENT_KIND_TAG,
                                                          FUNCTIONAL_REQUIREMENT_BIT)
                                                 : TagBit {};
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_target = item && (*item.value).kind == rule.target_kind;
        if (!is_target) { continue; }
        const bool can_read_tag = is_functional_only && functional.tag_index != MISSING;
        const uint64_t kind_value =
            can_read_tag ? _workspace.items.tags().value(handle, functional.tag_index) : 0;
        if (is_functional_only && (kind_value & functional.bit) == 0) { continue; }
        const size_t test_count =
            countRelations(rule.test_kind, relation, handle, RelationSide::TO);
        if (test_count != 1) { add(rule.pair_condition, (*item.value).id); }
    }
}

void Checker::checkSingleTargets(ItemKind test_kind, uint32_t relation)
{
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_test = item && (*item.value).kind == test_kind;
        if (!is_test) { continue; }
        const size_t target_count =
            countRelations(test_kind, relation, handle, RelationSide::FROM);
        if (target_count != 1) { add(KIOTTY_SPEC_CONDITION_SINGLE_TARGET, (*item.value).id); }
    }
}

uint32_t Checker::relationIndex(ItemKind kind, std::string_view name)
{
    const IndexResult found = _workspace.catalog.finder().findRelation(kind, spanOf(name));
    return found ? found.value : MISSING;
}

size_t Checker::countRelations(ItemKind model, uint32_t relation, ItemHandle item,
                               RelationSide side)
{
    size_t count = 0;
    for (const RelationRecord& record : _workspace.relations.all()) {
        if (record.model != model || record.relation != relation) { continue; }
        const ItemHandle endpoint = side == RelationSide::TO ? record.to : record.from;
        if (endpoint.index == item.index) { ++count; }
    }
    return count;
}

void Checker::checkResults()
{
    checkPassingResults();
    checkClosedChangeRequests();
}

void Checker::checkPassingResults()
{
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_result = item && isResultKind((*item.value).kind);
        if (!is_result) { continue; }
        const TagBit pass = tagBit((*item.value).kind, RESULT_TAG, PASS_BIT);
        const bool has_pass_tag = pass.tag_index != MISSING;
        const uint64_t result_value =
            has_pass_tag ? _workspace.items.tags().value(handle, pass.tag_index) : 0;
        if (pass.tag_index == MISSING || result_value != pass.bit) {
            add(KIOTTY_SPEC_CONDITION_PASSING_RESULT, (*item.value).id);
        }
    }
}

void Checker::checkClosedChangeRequests()
{
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.slots().handleAt(index);
        const auto item = _workspace.items.reader().get(handle);
        const bool is_change_request = item && (*item.value).kind == ItemKind::CHANGE_REQUEST;
        if (!is_change_request) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (is_open) { add(KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST, (*item.value).id); }
    }
}

bool Checker::isOpenChangeRequest(ItemHandle handle)
{
    const TagBit open =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_OPEN_BIT);
    const TagBit in_progress =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_IN_PROGRESS_BIT);
    if (open.tag_index == MISSING) { return true; }
    const uint64_t value = _workspace.items.tags().value(handle, open.tag_index);
    return value == 0 || (value & (open.bit | in_progress.bit)) != 0;
}

TagBit Checker::tagBit(ItemKind kind, std::string_view tag, std::string_view bit)
{
    const IndexResult found = _workspace.catalog.finder().findTag(kind, spanOf(tag));
    if (!found) { return TagBit{}; }
    const IndexResult position =
        _workspace.catalog.finder().findTagBit(kind, found.value, spanOf(bit));
    if (!position) { return TagBit{}; }
    return TagBit{uint64_t{1} << position.value, found.value};
}

void Checker::add(KiottySpecCondition condition, entity::IdText id)
{
    const Span<UnmetCondition> rows = viewVector(_rows);
    _length = appendCondition(rows, _length, UnmetCondition{id, condition});
}

namespace {

uint32_t findLowestStep(uint64_t value)
{
    for (uint32_t bit = 0; bit < entity::STEP_COUNT; ++bit) {
        const bool is_set = (value & (uint64_t{1} << bit)) != 0;
        if (is_set) { return bit + 1; }
    }
    return 0;
}

bool isResultKind(ItemKind kind)
{
    for (const ItemKind result_kind : RESULT_KINDS) {
        if (kind == result_kind) { return true; }
    }
    return false;
}

size_t appendCondition(Span<UnmetCondition> rows, size_t length, const UnmetCondition& row)
{
    const auto slot = rows.at(length);
    if (!slot) { return length; }
    *slot.value = row;
    return length + 1;
}

}

}
