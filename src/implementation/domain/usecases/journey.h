#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/repository/change_request_store.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/version_history.h"
#include "implementation/domain/entity/records.h"
#include "implementation/domain/usecases/checker.h"

#include <string>
#include <vector>

namespace kiotty::spec::implementation {

using PositionResult = Result<PositionRecord, KiottySpecStatus>;

struct NextMove {
    int32_t step = 1;
    std::string action;
};

using NextMoveResult = Result<NextMove, KiottySpecStatus>;
using StepRunListResult = Result<std::vector<StepRunRecord>, KiottySpecStatus>;

struct JourneySources {
    Meta &meta;
    CheckpointStore &checkpoints;
    VersionHistory &history;
    ItemFinder &finder;
    ChangeRequestStore &changes;
    Checker &checker;
    Failure &failure;
};

class Journey {
public:
    explicit Journey(JourneySources sources) : _s(sources) {}

    PositionResult position();
    StepRunListResult runs();
    TextResult whereAmI(size_t maxBytes);

private:
    NextMoveResult nextMove(const CheckpointRecord &latest, const std::vector<ChangeRequestRecord> &open);
    std::string reasonOf(const std::vector<StepRunRecord> &runs, const CheckpointRecord &cp, const std::vector<ChangeRequestRecord> &changes);
    TextResult recentChanges(const PositionRecord &position);
    ChangeRequestListResult openChanges();
    KiottySpecStatus assign(NoteText &target, const std::string &text);

    JourneySources _s;
};

}
