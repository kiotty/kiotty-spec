#pragma once

#include "implementation/core/database.h"
#include "implementation/core/records.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kiotty::base {

// 체크포인트: 라운드마다 하나. 미기록(PENDING) 쓰기를 새 체크포인트 번호로 묶는다.
class CheckpointStore {
public:
    explicit CheckpointStore(core::Database &db) : db_(db) {}

    int64_t create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose);
    std::optional<core::CheckpointRecord> find(int64_t seq);
    core::CheckpointRecord get(int64_t seq);
    std::optional<core::CheckpointRecord> latest();
    std::optional<core::CheckpointRecord> lastConfirmed(int32_t stepOr0);
    std::vector<core::CheckpointRecord> list(int32_t stepOr0);
    void markConfirmed(int64_t seq);

    // "CP-0042" 를 체크포인트 번호로. PENDING 은 kCurrent. 존재하지 않으면 NotFound.
    int64_t resolve(std::string_view cpId);

    size_t pendingWriteCount();
    size_t prune(int64_t baseSeq);

private:
    core::Database &db_;
};

} // namespace kiotty::base
