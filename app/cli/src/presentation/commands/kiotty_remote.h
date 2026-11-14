#pragma once

#include "presentation/commands/kiotty_commands.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::cli {

struct RemotePaths {
    std::filesystem::path kiotty {};
    std::filesystem::path directory {};
    std::filesystem::path rules {};
    std::filesystem::path commit {};
    std::filesystem::path exported {};
    std::filesystem::path work {};
};

struct RuleDiffRow {
    std::string id {};
    std::string title {};
    KiottySpecChange change {};
    uint32_t fields {0};
};

struct PrivateRuleRow {
    std::string private_rule {};
    RuleDiffRow rule {};
};

struct ReferrerRow {
    std::string item {};
    std::string relation {};
    std::string rule {};
    bool is_deleted {false};
};

struct AcceptImpact {
    std::vector<PrivateRuleRow> private_rules {};
    std::vector<ReferrerRow> referrers {};
};

struct AcceptTexts {
    std::string description {};
    std::string title {};
    std::string summary {};
    std::string body {};
    std::string rule_base {};
};

struct DiffCounts {
    size_t added {0};
    size_t modified {0};
    size_t removed {0};
};

using RemoteRulesCommand = int (*)(const Session& local, const Session& remote);

constexpr std::string_view RULE_FILE_NAME = "rule.kisp";

RemotePaths findRemotePaths(const Session& session);
bool prepareRemoteDirectory(const RemotePaths& paths);
bool storePulledRules(const RemotePaths& paths, const std::string& commit);
std::optional<std::string> readPulledCommit(const RemotePaths& paths);
void removePulledRules(const RemotePaths& paths);
std::optional<std::string> publishRules(const Session& session, const KiottySpecRemote& remote,
                                        const RemotePaths& paths);
std::optional<std::string> readRemoteCommit(const std::string& url);
std::optional<std::string> cloneRemote(const std::string& url, const RemotePaths& paths);
void removeQuietly(const std::filesystem::path& path);
bool writeText(const std::filesystem::path& path, std::string_view text);
std::string trimLine(std::string_view text);

std::optional<std::vector<RuleDiffRow>> readRuleDiffs(const Session& local,
                                                      const Session& remote);
std::vector<RuleDiffRow> filterRuleDiffs(const std::vector<RuleDiffRow>& rows,
                                         const std::vector<std::string>& wanted);
int printRuleDiffs(const Session& local, const Session& remote,
                   const std::vector<RuleDiffRow>& rows);
AcceptImpact collectImpact(const Session& session, const std::vector<RuleDiffRow>& rows);
AcceptTexts buildAcceptTexts(const KiottySpecRemote& remote, const std::string& commit,
                             const std::vector<RuleDiffRow>& rows, const AcceptImpact& impact);
void printAcceptResult(const KiottySpecAcceptResult& accepted, const AcceptImpact& impact);
DiffCounts countDiffs(const std::vector<RuleDiffRow>& rows);
std::string formatShortCommit(std::string_view commit);

}
