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

using IndexResult = Result<uint32_t, KiottySpecStatus>;

constexpr uint32_t MISSING = 0xFFFFFFFFu;
constexpr ItemKind RESULT_KINDS[] = { ItemKind::SYSTEM_TEST_RESULT,
                                      ItemKind::END_TO_END_TEST_RESULT,
                                      ItemKind::CODE_INSPECTION_RESULT };

uint32_t findLowestStep(uint64_t value);
bool isResultKind(ItemKind kind);
size_t appendCondition(Span<UnmetCondition> rows, size_t length, const UnmetCondition& row);
uint64_t readValue(Span<const uint64_t> values, size_t index);
uint32_t readCount(Span<const uint32_t> counts, size_t index);
void clearValues(Span<uint64_t> values);

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
    const TagBit open = openBits();
    const Span<const uint64_t> states = loadStates(open.tag_index);
    size_t count = 0;
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_open = isOpenAt(index, states, open);
        if (is_open) { ++count; }
    }
    return count;
}

uint32_t Checker::earliestEntryStep()
{
    const IndexResult entry = _workspace.catalog.finder().findTag(
        ItemKind::CHANGE_REQUEST, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    if (!entry) { return 0; }
    const TagBit open = openBits();
    const Span<const uint64_t> states = loadStates(open.tag_index);
    const Span<const uint64_t> entry_steps = loadTags(ItemKind::CHANGE_REQUEST, entry.value);
    uint32_t earliest = 0;
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_open = isOpenAt(index, states, open);
        if (!is_open) { continue; }
        const uint64_t entry_step = readValue(entry_steps, index);
        const uint32_t step = findLowestStep(entry_step);
        if (step != 0 && (earliest == 0 || step < earliest)) { earliest = step; }
    }
    return earliest;
}

void Checker::checkEntrySteps()
{
    const IndexResult entry = _workspace.catalog.finder().findTag(
        ItemKind::CHANGE_REQUEST, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    const uint32_t entry_tag = entry ? entry.value : MISSING;
    const TagBit open = openBits();
    const Span<const uint64_t> states = loadStates(open.tag_index);
    const Span<const uint64_t> entry_steps = loadTags(ItemKind::CHANGE_REQUEST, entry_tag);
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_open = isOpenAt(index, states, open);
        if (!is_open) { continue; }
        const uint64_t entry_step = readValue(entry_steps, index);
        if (entry_step != 0) { continue; }
        add(KIOTTY_SPEC_CONDITION_ENTRY_STEP, index);
    }
}

void Checker::checkAcceptanceCriteria()
{
    const TagBit functional =
        tagBit(ItemKind::REQUIREMENT, REQUIREMENT_KIND_TAG, FUNCTIONAL_REQUIREMENT_BIT);
    if (functional.tag_index == MISSING) { return; }
    const Span<const uint64_t> kinds = loadTags(ItemKind::REQUIREMENT, functional.tag_index);
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_requirement = readKind(index) == ItemKind::REQUIREMENT;
        if (!is_requirement) { continue; }
        const uint64_t kind_value = readValue(kinds, index);
        if ((kind_value & functional.bit) == 0) { continue; }
        const bool has_criteria = hasAcceptanceCriteria(index);
        if (has_criteria) { continue; }
        add(KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA, index);
    }
}

bool Checker::hasAcceptanceCriteria(size_t index)
{
    const ItemHandle handle = _workspace.items.slots().handleAt(index);
    const Span<char> body = viewVector(_body);
    const Result<size_t, KiottySpecStatus> length =
        _workspace.items.reader().readBody(handle, body);
    const auto text = body.subspan(0, length.value);
    const size_t criteria_count = length ? countAcceptanceCriteria(text.value) : 0;
    return criteria_count != 0;
}

void Checker::checkMilestoneCoverage()
{
    const uint32_t designs = relationIndex(ItemKind::MILESTONE, MILESTONE_DESIGN_RELATION);
    const uint32_t tests = relationIndex(ItemKind::MILESTONE, MILESTONE_UNIT_TEST_RELATION);
    const Span<const uint32_t> design_counts =
        loadRelationCounts(RelationEnd {ItemKind::MILESTONE, designs, RelationSide::TO});
    const Span<const uint32_t> test_counts =
        loadSecondRelationCounts(RelationEnd {ItemKind::MILESTONE, tests, RelationSide::TO});
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemKind kind = readKind(index);
        const bool is_design = kind == ItemKind::DESIGN && designs != MISSING;
        const bool is_test = kind == ItemKind::UNIT_TEST_PLAN && tests != MISSING;
        const bool is_covered_kind = is_design || is_test;
        if (!is_covered_kind) { continue; }
        const uint32_t count =
            is_design ? readCount(design_counts, index) : readCount(test_counts, index);
        if (count == 0) { add(KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE, index); }
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
    const Span<const uint64_t> kinds = loadTags(ItemKind::REQUIREMENT, functional.tag_index);
    const Span<const uint32_t> test_counts =
        loadRelationCounts(RelationEnd {rule.test_kind, relation, RelationSide::TO});
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_target = readKind(index) == rule.target_kind;
        if (!is_target) { continue; }
        const uint64_t kind_value = readValue(kinds, index);
        if (is_functional_only && (kind_value & functional.bit) == 0) { continue; }
        const uint32_t test_count = readCount(test_counts, index);
        if (test_count != 1) { add(rule.pair_condition, index); }
    }
}

void Checker::checkSingleTargets(ItemKind test_kind, uint32_t relation)
{
    const Span<const uint32_t> target_counts =
        loadRelationCounts(RelationEnd {test_kind, relation, RelationSide::FROM});
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_test = readKind(index) == test_kind;
        if (!is_test) { continue; }
        const uint32_t target_count = readCount(target_counts, index);
        if (target_count != 1) { add(KIOTTY_SPEC_CONDITION_SINGLE_TARGET, index); }
    }
}

uint32_t Checker::relationIndex(ItemKind kind, std::string_view name)
{
    const IndexResult found = _workspace.catalog.finder().findRelation(kind, spanOf(name));
    return found ? found.value : MISSING;
}

void Checker::checkResults()
{
    checkPassingResults();
    checkClosedChangeRequests();
}

void Checker::checkPassingResults()
{
    const Span<const uint64_t> results = loadResultTags();
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemKind kind = readKind(index);
        const bool is_result = isResultKind(kind);
        if (!is_result) { continue; }
        const TagBit pass = tagBit(kind, RESULT_TAG, PASS_BIT);
        const uint64_t result_value = readValue(results, index);
        if (pass.tag_index == MISSING || result_value != pass.bit) {
            add(KIOTTY_SPEC_CONDITION_PASSING_RESULT, index);
        }
    }
}

void Checker::checkClosedChangeRequests()
{
    const TagBit open = openBits();
    const Span<const uint64_t> states = loadStates(open.tag_index);
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_open = isOpenAt(index, states, open);
        if (is_open) { add(KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST, index); }
    }
}

bool Checker::isOpenAt(size_t index, Span<const uint64_t> states, TagBit open) const
{
    const bool is_change_request = readKind(index) == ItemKind::CHANGE_REQUEST;
    if (!is_change_request) { return false; }
    if (open.tag_index == MISSING) { return true; }
    const uint64_t value = readValue(states, index);
    return value == 0 || (value & open.bit) != 0;
}

TagBit Checker::openBits()
{
    const TagBit open =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_OPEN_BIT);
    const TagBit in_progress =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_IN_PROGRESS_BIT);
    return TagBit {open.bit | in_progress.bit, open.tag_index};
}

ItemKind Checker::readKind(size_t index) const
{
    const ItemHandle handle = _workspace.items.slots().handleAt(index);
    const auto item = _workspace.items.reader().get(handle);
    if (!item) { return ItemKind {}; }
    return (*item.value).kind;
}

Span<const uint64_t> Checker::loadStates(uint32_t tag)
{
    const Span<uint64_t> values = viewVector(_state_values);
    clearValues(values);
    _workspace.items.tags().copyValues(ItemKind::CHANGE_REQUEST, tag, values);
    return values;
}

Span<const uint64_t> Checker::loadTags(ItemKind model, uint32_t tag)
{
    const Span<uint64_t> values = viewVector(_tag_values);
    clearValues(values);
    _workspace.items.tags().copyValues(model, tag, values);
    return values;
}

Span<const uint64_t> Checker::loadResultTags()
{
    const Span<uint64_t> values = viewVector(_tag_values);
    clearValues(values);
    for (const ItemKind kind : RESULT_KINDS) {
        const TagBit pass = tagBit(kind, RESULT_TAG, PASS_BIT);
        _workspace.items.tags().copyValues(kind, pass.tag_index, values);
    }
    return values;
}

Span<const uint32_t> Checker::loadRelationCounts(RelationEnd end)
{
    const Span<uint32_t> counts = viewVector(_relation_counts);
    _workspace.relations.countEnds(end, counts);
    return counts;
}

Span<const uint32_t> Checker::loadSecondRelationCounts(RelationEnd end)
{
    const Span<uint32_t> counts = viewVector(_second_relation_counts);
    _workspace.relations.countEnds(end, counts);
    return counts;
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

void Checker::add(KiottySpecCondition condition, size_t index)
{
    const ItemHandle handle = _workspace.items.slots().handleAt(index);
    const auto label = _workspace.items.reader().label(handle);
    if (!label) { return; }
    const Span<UnmetCondition> rows = viewVector(_rows);
    _length = appendCondition(rows, _length, UnmetCondition{(*label.value).id, condition});
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

uint64_t readValue(Span<const uint64_t> values, size_t index)
{
    const Result<const uint64_t&, KiottySpecStatus> value = values.at(index);
    return value ? *value.value : 0;
}

uint32_t readCount(Span<const uint32_t> counts, size_t index)
{
    const Result<const uint32_t&, KiottySpecStatus> count = counts.at(index);
    return count ? *count.value : 0;
}

void clearValues(Span<uint64_t> values)
{
    for (uint64_t& value : values) { value = 0; }
}

}

}
