#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/store_result.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace kiotty::spec::implementation {

struct MilestoneState {
    KiottySpecImplementationState state = KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED;
    std::optional<int64_t> implementedWseq;
    std::optional<int64_t> implementedCpSeq;
};

using OptionalMilestoneStateResult = Result<std::optional<MilestoneState>, KiottySpecStatus>;

class MilestoneStore {
public:
    explicit MilestoneStore(Storage &storage) : _storage(storage) {}

    KiottySpecStatus write(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state,
                           std::optional<int64_t> implementedWseq);
    OptionalMilestoneStateResult latest(std::string_view milestoneId, std::string_view itemId, int64_t cut);

private:
    Storage &_storage;
};

}
