#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/relation/kiotty_relation_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct UnmetCondition {
    entity::IdText id {};
    KiottySpecCondition condition {KIOTTY_SPEC_CONDITION_ENTRY_STEP};
};

static_assert(sizeof(UnmetCondition) == 20, "unexpected padding");

struct TagBit {
    static constexpr uint32_t MISSING = 0xFFFFFFFFu;

    uint64_t bit {0};
    uint32_t tag_index {MISSING};
};

static_assert(sizeof(TagBit) == 16, "unexpected padding");

enum class TargetFilter : uint32_t {
    ALL_TARGETS = 1,
    FUNCTIONAL_REQUIREMENTS,
    TOP_LEVEL_TARGETS
};

struct PairRule {
    std::string_view relation_name {};
    entity::ItemKind target_kind {};
    entity::ItemKind test_kind {};
    KiottySpecCondition pair_condition {KIOTTY_SPEC_CONDITION_ENTRY_STEP};
    TargetFilter filter {TargetFilter::ALL_TARGETS};
};

static_assert(sizeof(PairRule) == 32, "unexpected padding");

class Checker
{
public:
    Checker(Workspace& workspace, size_t max_items)
        : _workspace(workspace), _body(entity::MAX_BODY_LENGTH + 1), _rows(max_items),
          _state_values(max_items, 0), _tag_values(max_items, 0), _relation_counts(max_items, 0),
          _second_relation_counts(max_items, 0)
    {}

    Span<const UnmetCondition> collect(entity::Step step);

    size_t openChangeRequestCount();
    uint32_t earliestEntryStep();

private:
    TagBit tagBit(entity::ItemKind kind, std::string_view tag, std::string_view bit);
    TagBit openBits();
    uint32_t relationIndex(entity::ItemKind kind, std::string_view name);
    entity::ItemKind readKind(size_t index) const;
    bool hasParent(size_t index) const;
    bool isOpenAt(size_t index, Span<const uint64_t> states, TagBit open) const;
    Span<const uint64_t> loadStates(uint32_t tag);
    Span<const uint64_t> loadTags(entity::ItemKind model, uint32_t tag);
    Span<const uint64_t> loadResultTags();
    Span<const uint32_t> loadRelationCounts(RelationEnd end);
    Span<const uint32_t> loadSecondRelationCounts(RelationEnd end);
    void add(KiottySpecCondition condition, size_t index);

    void checkEntrySteps();
    void checkAcceptanceCriteria();
    bool hasAcceptanceCriteria(size_t index);
    void checkMilestoneCoverage();
    void checkOneToOne(const PairRule& rule);
    void checkTargetPairs(const PairRule& rule, uint32_t relation);
    void checkSingleTargets(entity::ItemKind test_kind, uint32_t relation);
    void checkResults();
    void checkPassingResults();
    void checkClosedChangeRequests();

    Workspace& _workspace;
    std::vector<char> _body;
    std::vector<UnmetCondition> _rows;
    std::vector<uint64_t> _state_values;
    std::vector<uint64_t> _tag_values;
    std::vector<uint32_t> _relation_counts;
    std::vector<uint32_t> _second_relation_counts;
    size_t _length {0};
};

}
