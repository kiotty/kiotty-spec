#pragma once

#include "kiotty_spec.h"

#include <cstdint>
#include <optional>
#include <string>

namespace kiotty::spec::implementation {

struct ItemRecord {
    std::string id;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category = KIOTTY_SPEC_ITEM_CATEGORY_NONE;
    std::string parentId;
    int32_t seq = 0;
    std::string title;
    std::string body;
    int64_t cpSeq = 0;
    int64_t wseq = 0;
    bool removed = false;
};

struct LinkRecord {
    std::string fromId;
    std::string toId;
    KiottySpecLinkType type{};
    int64_t cpSeq = 0;
};

struct FileRecord {
    std::string id;
    KiottySpecFileRole role{};
    std::string path;
    std::string hash;
    int64_t cpSeq = 0;
};

struct CheckpointRecord {
    int64_t seq = 0;
    int32_t step = 0;
    bool confirmed = false;
    KiottySpecCheckpointPurpose purpose = KIOTTY_SPEC_CHECKPOINT_PURPOSE_NONE;
    std::string note;
    int64_t createdAt = 0;
};

struct ChangeRequestRecord {
    std::string id;
    KiottySpecChangeSource source{};
    std::string body;
    int32_t entryStep = 0;
    KiottySpecChangeState state{};
    int64_t cpOpened = 0;
    std::optional<int64_t> cpClosed;
    std::string note;
};

struct ResultRecord {
    std::string targetId;
    KiottySpecTestResult result{};
    std::string log;
    int64_t cpSeq = 0;
    int64_t runAt = 0;
};

struct MilestoneEntryRecord {
    std::string milestoneId;
    std::string itemId;
    KiottySpecImplementationState state = KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED;
    std::optional<int64_t> implementedCpSeq;
};

struct StepRunRecord {
    int32_t step = 0;
    int64_t firstSeq = 0;
    int64_t lastSeq = 0;
    std::optional<int64_t> confirmedSeq;
    size_t count = 0;
    std::string lastNote;
    std::string reason;
};

struct PositionRecord {
    std::optional<int64_t> checkpointSeq;
    int32_t step = 0;
    bool confirmed = false;
    bool released = false;
    size_t pendingWrites = 0;
    int32_t nextStep = 1;
    std::string nextAction;
};

}
