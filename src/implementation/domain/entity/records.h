#pragma once

#include "kiotty_spec.h"
#include "implementation/core/fixed_string.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace kiotty::spec::implementation {

constexpr size_t MAX_ID_LENGTH = 16;
constexpr size_t MAX_TITLE_LENGTH = 256;
constexpr size_t MAX_PATH_LENGTH = 512;
constexpr size_t MAX_HASH_LENGTH = 16;
constexpr size_t MAX_NOTE_LENGTH = 512;

using IdText = FixedString<MAX_ID_LENGTH>;
using TitleText = FixedString<MAX_TITLE_LENGTH>;
using PathText = FixedString<MAX_PATH_LENGTH>;
using HashText = FixedString<MAX_HASH_LENGTH>;
using NoteText = FixedString<MAX_NOTE_LENGTH>;

struct ItemRecord {
    IdText id;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category = KIOTTY_SPEC_ITEM_CATEGORY_NONE;
    IdText parentId;
    int32_t seq = 0;
    TitleText title;
    std::string body;
    int64_t cpSeq = 0;
    int64_t wseq = 0;
    bool removed = false;
};

struct LinkRecord {
    IdText fromId;
    IdText toId;
    KiottySpecLinkType type{};
    int64_t cpSeq = 0;
};

struct FileRecord {
    IdText id;
    KiottySpecFileRole role{};
    PathText path;
    HashText hash;
    int64_t cpSeq = 0;
};

struct CheckpointRecord {
    int64_t seq = 0;
    int32_t step = 0;
    bool confirmed = false;
    KiottySpecCheckpointPurpose purpose = KIOTTY_SPEC_CHECKPOINT_PURPOSE_NONE;
    NoteText note;
    int64_t createdAt = 0;
};

struct ChangeRequestRecord {
    IdText id;
    KiottySpecChangeSource source{};
    std::string body;
    int32_t entryStep = 0;
    KiottySpecChangeState state{};
    int64_t cpOpened = 0;
    std::optional<int64_t> cpClosed;
    NoteText note;
};

struct ResultRecord {
    IdText targetId;
    KiottySpecTestResult result{};
    std::string log;
    int64_t cpSeq = 0;
    int64_t runAt = 0;
};

struct MilestoneEntryRecord {
    IdText milestoneId;
    IdText itemId;
    KiottySpecImplementationState state = KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED;
    std::optional<int64_t> implementedCpSeq;
};

struct StepRunRecord {
    int32_t step = 0;
    int64_t firstSeq = 0;
    int64_t lastSeq = 0;
    std::optional<int64_t> confirmedSeq;
    size_t count = 0;
    NoteText lastNote;
    NoteText reason;
};

struct PositionRecord {
    std::optional<int64_t> checkpointSeq;
    int32_t step = 0;
    bool confirmed = false;
    bool released = false;
    size_t pendingWrites = 0;
    int32_t nextStep = 1;
    NoteText nextAction;
};

}
