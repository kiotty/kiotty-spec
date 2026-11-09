#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/version_history.h"
#include "implementation/domain/usecases/checker.h"

#include <string_view>

namespace kiotty::spec::implementation {

struct LifecycleSources {
    Storage &storage;
    CheckpointStore &checkpoints;
    VersionHistory &history;
    ItemStore &items;
    LinkStore &links;
    Checker &checker;
    Failure &failure;
};

class Lifecycle {
public:
    explicit Lifecycle(LifecycleSources sources) : _s(sources) {}

    SequenceResult confirm(KiottySpecActor actor, std::string_view cpId);
    SequenceResult release(KiottySpecActor actor, std::string_view note);
    CountResult prune(KiottySpecActor actor, std::string_view baseCpId);
    KiottySpecStatus removeItem(std::string_view itemId);

private:
    KiottySpecStatus requireUser(KiottySpecActor actor, Span<const char> what);
    KiottySpecStatus rejectVerdict(Span<const char> heading, const Verdict &verdict);

    LifecycleSources _s;
};

}
