#include "implementation/domain/usecases/checker/kiotty_checker.h"

#include "implementation/domain/usecases/checker/kiotty_check_values.h"

namespace kiotty::spec::implementation {

namespace {

using check_values::readCount;
using check_values::readValue;
using entity::ItemKind;

constexpr uint32_t ONE_PAIR = 1;

}

void Checker::checkOneToOne(const PairRule& rule)
{
    const uint32_t relation = relationIndex(rule.test_kind, rule.relation_name);
    checkTargetPairs(rule, relation);
    checkSingleTargets(rule.test_kind, relation);
}

void Checker::checkCodeInspection()
{
    const ItemKind result = ItemKind::CODE_INSPECTION_RESULT;
    const uint32_t rule_relation = relationIndex(result, CODE_INSPECTION_RULE_RELATION);
    const uint32_t private_relation = relationIndex(result, CODE_INSPECTION_PRIVATE_RULE_RELATION);
    const uint32_t replaced = relationIndex(ItemKind::PRIVATE_RULE, REPLACED_RULE_RELATION);
    checkRulePairs(rule_relation, replaced);
    checkTargetPairs(PairRule { CODE_INSPECTION_PRIVATE_RULE_RELATION, ItemKind::PRIVATE_RULE,
                                result, KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR,
                                TargetFilter::ALL_TARGETS },
                     private_relation);
    checkInspectionTargets(rule_relation, private_relation);
}

void Checker::checkRulePairs(uint32_t relation, uint32_t replaced)
{
    const Span<const uint32_t> result_counts = loadRelationCounts(
        RelationEnd {ItemKind::CODE_INSPECTION_RESULT, relation, RelationSide::TO});
    const Span<const uint32_t> replaced_counts =
        loadSecondRelationCounts(RelationEnd {ItemKind::PRIVATE_RULE, replaced, RelationSide::TO});
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_rule = readKind(index) == ItemKind::RULE;
        if (!is_rule) { continue; }
        const bool is_replaced = readCount(replaced_counts, index) > 0;
        if (is_replaced) { continue; }
        const uint32_t result_count = readCount(result_counts, index);
        if (result_count != ONE_PAIR) { add(KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR, index); }
    }
}

void Checker::checkTargetPairs(const PairRule& rule, uint32_t relation)
{
    const bool is_functional_only = rule.filter == TargetFilter::FUNCTIONAL_REQUIREMENTS;
    const bool is_top_level_only = rule.filter == TargetFilter::TOP_LEVEL_TARGETS;
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
        const bool is_child = is_top_level_only && hasParent(index);
        if (is_child) { continue; }
        const uint32_t test_count = readCount(test_counts, index);
        if (test_count != ONE_PAIR) { add(rule.pair_condition, index); }
    }
}

void Checker::checkInspectionTargets(uint32_t rule_relation, uint32_t private_relation)
{
    const ItemKind result = ItemKind::CODE_INSPECTION_RESULT;
    const Span<const uint32_t> rule_counts =
        loadRelationCounts(RelationEnd {result, rule_relation, RelationSide::FROM});
    const Span<const uint32_t> private_counts =
        loadSecondRelationCounts(RelationEnd {result, private_relation, RelationSide::FROM});
    const size_t capacity = _workspace.items.slots().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const bool is_result = readKind(index) == result;
        if (!is_result) { continue; }
        const uint32_t target_count =
            readCount(rule_counts, index) + readCount(private_counts, index);
        if (target_count != ONE_PAIR) { add(KIOTTY_SPEC_CONDITION_SINGLE_TARGET, index); }
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
        if (target_count != ONE_PAIR) { add(KIOTTY_SPEC_CONDITION_SINGLE_TARGET, index); }
    }
}

}
