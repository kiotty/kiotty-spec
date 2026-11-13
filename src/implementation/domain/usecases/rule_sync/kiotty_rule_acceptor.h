#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_compare.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct AcceptDraft {
    Span<const char> description {};
    Span<const char> title {};
    Span<const char> summary {};
    Span<const char> body {};
    Span<const char> rule_base {};
    entity::Step entry_step {entity::Step::CODE_INSPECTING};
};

struct AcceptOutcome {
    entity::IdText change_request {};
    uint32_t checkpoint {0};
};

struct RuleChange {
    entity::ItemHandle local {};
    entity::ItemHandle remote {};
    uint32_t change {0};
    uint32_t fields {0};
};

static_assert(sizeof(AcceptDraft) == 88, "unexpected padding");
static_assert(sizeof(AcceptOutcome) == 20, "unexpected padding");

class RuleAcceptor
{
public:
    RuleAcceptor(Workspace& local, size_t max_items)
        : _local(local), _changes(max_items), _marks(max_items)
    {}

    Result<AcceptOutcome, KiottySpecStatus> accept(Workspace& remote, const AcceptDraft& draft,
                                                   entity::Actor actor);

private:
    KiottySpecStatus checkAccept(Workspace& remote, entity::Actor actor);
    KiottySpecStatus collectChanges(Workspace& remote);
    Result<AcceptOutcome, KiottySpecStatus> applyAll(Workspace& remote, const AcceptDraft& draft);
    Result<entity::ItemHandle, KiottySpecStatus> createRequest(const AcceptDraft& draft,
                                                               uint32_t sequence);
    KiottySpecStatus tagRequest(entity::ItemHandle request, entity::Step entry_step,
                                uint32_t sequence);
    KiottySpecStatus copyRules(Workspace& remote, uint32_t sequence);
    KiottySpecStatus copyRule(RuleSide remote, RuleChange& change, uint32_t sequence);
    KiottySpecStatus copyContent(RuleSide remote, entity::ItemHandle local, uint32_t sequence);
    KiottySpecStatus linkParents(Workspace& remote, uint32_t sequence);
    KiottySpecStatus linkRequest(entity::ItemHandle request, uint32_t sequence);
    KiottySpecStatus linkPrivateRules(entity::ItemHandle request, uint32_t sequence);
    KiottySpecStatus removeResults(uint32_t sequence);
    KiottySpecStatus removeRules(uint32_t sequence);
    bool isChangedTarget(entity::ItemHandle rule) const;
    uint32_t relationOf(entity::ItemKind kind, std::string_view name) const;

    Workspace& _local;
    std::vector<RuleChange> _changes;
    std::vector<entity::ItemHandle> _marks;
    size_t _change_count {0};
};

}
