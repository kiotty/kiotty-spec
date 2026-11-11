#pragma once

#include "kiotty_spec.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiotty::cli {

struct Arguments {
    std::vector<std::string> positional {};
    std::vector<std::pair<std::string, std::string>> options {};
    std::vector<std::string> flags {};

    std::optional<std::string> option(std::string_view name) const;
    std::vector<std::string> all(std::string_view name) const;
    bool flag(std::string_view name) const;
};

Arguments parseArguments(const std::vector<std::string>& tokens, size_t first,
                         const std::vector<std::string_view>& extra_flags);

struct Session {
    KiottySpecContext* context {nullptr};
    KiottySpecActor actor {KIOTTY_SPEC_ACTOR_AI};
    Arguments args {};
};

constexpr int USAGE_EXIT_CODE = 2;
constexpr int DECIMAL_BASE = 10;

int fail(Session& session, KiottySpecStatus status);
int usage(const std::string& message);

KiottySpecText textOf(std::string_view text);
std::string kindName(KiottySpecItemKind kind);
std::string stateName(KiottySpecCheckpointState state);
std::string cell(std::string_view value);
void printRow(const std::vector<std::string>& cells);

std::string checkpointName(KiottySpecCheckpointHandle handle);
std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text);
std::optional<std::string> readFileText(const std::string& path);
std::optional<KiottySpecItemKind> kindArgument(Session& session, std::string_view text);
std::string itemId(Session& session, KiottySpecItemHandle handle);

struct TagColumn {
    uint32_t index {};
    KiottySpecTagDefinition definition {};
};

std::optional<TagColumn> findTag(Session& session, KiottySpecItemKind kind, std::string_view name);
std::optional<uint32_t> findRelation(Session& session, KiottySpecItemKind kind,
                                     std::string_view name);
std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition,
                                  std::string_view names);
std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value);

using Command = int (*)(Session&);

struct CommandEntry {
    const char* group {};
    const char* verb {};
    const char* usage {};
    const char* summary {};
    Command run {};
};

const std::vector<CommandEntry>& commands();

int runDiff(Session& session);
extern const std::vector<std::string_view> DIFF_FLAGS;

}
