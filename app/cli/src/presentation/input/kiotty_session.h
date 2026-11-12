#pragma once

#include "kiotty_spec.h"
#include "i18n/kiotty_messages.h"

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
    KiottySpecContext& context;
    KiottySpecActor actor {KIOTTY_SPEC_ACTOR_AI};
    Arguments arguments {};
};

constexpr int USAGE_EXIT_CODE = 2;
constexpr int DECIMAL_BASE = 10;

int fail(const Session& session, KiottySpecStatus status);
int failWithoutContext(KiottySpecStatus status);
int usage(std::string_view message);

KiottySpecText textOf(std::string_view text);
std::string kindName(KiottySpecItemKind kind);
std::string stateName(KiottySpecCheckpointState state);
std::string cell(std::string_view value);
std::string quote(std::string_view text);
std::string escape(std::string_view text);
void printRow(const std::vector<std::string>& cells);

std::string checkpointName(KiottySpecCheckpointHandle handle);
std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text);
std::optional<uint32_t> parseNumber(std::string_view text);
std::optional<KiottySpecActor> parseActor(std::string_view text);
std::optional<std::string> readFileText(const std::string& path);
std::optional<KiottySpecItemKind> kindArgument(const Session& session, std::string_view text);
std::string itemId(const Session& session, KiottySpecItemHandle handle);

struct TagColumn {
    uint32_t index {};
    KiottySpecTagDefinition definition {};
};

std::vector<TagColumn> readTagDefinitions(const Session& session, KiottySpecItemKind kind);
std::vector<KiottySpecRelationDefinition> readRelationDefinitions(const Session& session,
                                                                  KiottySpecItemKind kind);
std::string findRelationName(const Session& session, KiottySpecItemKind kind, uint32_t relation);
std::optional<TagColumn> findTag(const Session& session, KiottySpecItemKind kind,
                                 std::string_view name);
std::optional<uint32_t> findRelation(const Session& session, KiottySpecItemKind kind,
                                     std::string_view name);
std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition,
                                  std::string_view names);
std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value);
bool detectColorTerminal();

using Command = int (*)(const Session&);

struct CommandEntry {
    std::string_view group {};
    std::string_view verb {};
    MessageKey usage {};
    MessageKey summary {};
    Command run {};
};

const std::vector<CommandEntry>& commands();

int runDiff(const Session& session);
extern const std::vector<std::string_view> DIFF_FLAGS;

}
