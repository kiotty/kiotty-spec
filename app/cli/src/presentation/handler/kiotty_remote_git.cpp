#include "presentation/commands/kiotty_remote.h"

#include "infra/kiotty_git.h"
#include "infra/kiotty_path_utf8.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>

namespace kiotty::cli {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view COMMIT_FILE_NAME = "rule.commit";
constexpr std::string_view EXPORT_FILE_NAME = "push.kisp";
constexpr std::string_view IGNORED_ENTRY = "remote/";
constexpr std::string_view BRANCH_PREFIX = "refs/heads/";
constexpr std::string_view LINE_SPACES = " \t\r\n";
constexpr std::chrono::seconds GIT_LIMIT {300};
constexpr size_t DIRECTORY_VERB_POSITION = 2;

bool ensureIgnored(const RemotePaths& paths);
bool hasLine(std::string_view text, std::string_view wanted);
std::optional<std::string> commitAndPush(const Session& session, const RemotePaths& paths);
bool commitRules(const Session& session, const std::string& work);
std::string findProjectName(const Session& session);
std::string findHeadCommit(std::string_view listed);
std::optional<std::string> requireGit(const std::vector<std::string>& arguments);
GitRun runGitCommand(const std::vector<std::string>& arguments);
std::string describeGitVerb(const std::vector<std::string>& arguments);

}

RemotePaths findRemotePaths(const Session& session)
{
    RemotePaths paths {};
    paths.kiotty = pathFromUtf8(session.root) / ".kiotty";
    paths.directory = paths.kiotty / "remote";
    paths.rules = paths.directory / RULE_FILE_NAME;
    paths.commit = paths.directory / COMMIT_FILE_NAME;
    paths.exported = paths.directory / EXPORT_FILE_NAME;
    paths.work = paths.directory / "work";
    return paths;
}

bool prepareRemoteDirectory(const RemotePaths& paths)
{
    std::error_code error {};
    fs::remove_all(paths.work, error);
    fs::create_directories(paths.directory, error);
    if (error) {
        failMessage(MessageKey::REMOTE_FILE_UNWRITABLE, {pathToUtf8(paths.directory)});
        return false;
    }
    return ensureIgnored(paths);
}

bool storePulledRules(const RemotePaths& paths, const std::string& commit)
{
    const fs::path fetched = paths.work / RULE_FILE_NAME;
    std::error_code error {};
    const bool has_rules = fs::is_regular_file(fetched, error);
    if (!has_rules) {
        failMessage(MessageKey::REMOTE_RULE_FILE_MISSING, {});
        return false;
    }
    fs::copy_file(fetched, paths.rules, fs::copy_options::overwrite_existing, error);
    const bool is_copied = !error;
    const bool is_saved = is_copied && writeText(paths.commit, commit + "\n");
    if (!is_saved) { failMessage(MessageKey::REMOTE_FILE_UNWRITABLE, {pathToUtf8(paths.rules)}); }
    return is_saved;
}

std::optional<std::string> readPulledCommit(const RemotePaths& paths)
{
    const std::optional<std::string> text = readFileText(pathToUtf8(paths.commit));
    if (!text) {
        failMessage(MessageKey::REMOTE_RULES_MISSING, {});
        return std::nullopt;
    }
    return trimLine(text.value());
}

void removePulledRules(const RemotePaths& paths)
{
    removeQuietly(paths.rules);
    removeQuietly(paths.commit);
    removeQuietly(paths.work);
}

std::optional<std::string> publishRules(const Session& session, const KiottySpecRemote& remote,
                                        const RemotePaths& paths)
{
    const std::string exported = pathToUtf8(paths.exported);
    const KiottySpecStatus written =
        KiottySpec_exportRules(&session.context, textOf(exported)).code;
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, written);
        return std::nullopt;
    }
    const std::optional<std::string> current = readRemoteCommit(remote.url);
    if (!current) { return std::nullopt; }
    const bool is_moved = !current.value().empty() && current.value() != remote.rule_base;
    if (is_moved) {
        failMessage(MessageKey::REMOTE_CHANGED, {current.value()});
        return std::nullopt;
    }
    const std::optional<std::string> cloned = cloneRemote(remote.url, paths);
    if (!cloned) { return std::nullopt; }
    std::error_code error {};
    fs::rename(paths.exported, paths.work / RULE_FILE_NAME, error);
    if (error) {
        failMessage(MessageKey::REMOTE_FILE_UNWRITABLE, {pathToUtf8(paths.work)});
        return std::nullopt;
    }
    return commitAndPush(session, paths);
}

std::optional<std::string> readRemoteCommit(const std::string& url)
{
    const std::optional<std::string> listed = requireGit({"ls-remote", url});
    if (!listed) { return std::nullopt; }
    return findHeadCommit(listed.value());
}

std::optional<std::string> cloneRemote(const std::string& url, const RemotePaths& paths)
{
    const std::string work = pathToUtf8(paths.work);
    const std::optional<std::string> cloned = requireGit(
        {"clone", "--quiet", "--depth", "1", "--config", "core.autocrlf=false", url, work});
    if (!cloned) { return std::nullopt; }
    const GitRun head = runGit({"-C", work, "rev-parse", "--verify", "--quiet", "HEAD"}, GIT_LIMIT);
    const bool has_head = head.outcome == GitOutcome::FINISHED && head.exit_code == 0;
    if (!has_head) { return std::string(); }
    return trimLine(head.output);
}

void removeQuietly(const fs::path& path)
{
    std::error_code error {};
    fs::remove_all(path, error);
}

namespace {

bool ensureIgnored(const RemotePaths& paths)
{
    const fs::path ignore = paths.kiotty / ".gitignore";
    const std::string current = readFileText(pathToUtf8(ignore)).value_or("");
    const bool is_listed = hasLine(current, IGNORED_ENTRY);
    if (is_listed) { return true; }
    const bool has_open_line = !current.empty() && current.back() != '\n';
    const std::string separator = has_open_line ? "\n" : "";
    const std::string entry = separator + std::string(IGNORED_ENTRY) + "\n";
    const bool is_written = writeText(ignore, current + entry);
    if (!is_written) { failMessage(MessageKey::REMOTE_FILE_UNWRITABLE, {pathToUtf8(ignore)}); }
    return is_written;
}

bool hasLine(std::string_view text, std::string_view wanted)
{
    size_t start = 0;
    const size_t length = text.size();
    while (start < length) {
        const size_t end = std::min(text.find('\n', start), length);
        const bool is_match = trimLine(text.substr(start, end - start)) == wanted;
        if (is_match) { return true; }
        start = end + 1;
    }
    return false;
}

std::optional<std::string> commitAndPush(const Session& session, const RemotePaths& paths)
{
    const std::string work = pathToUtf8(paths.work);
    const std::optional<std::string> added =
        requireGit({"-C", work, "add", std::string(RULE_FILE_NAME)});
    if (!added) { return std::nullopt; }
    const GitRun staged = runGitCommand({"-C", work, "diff", "--cached", "--quiet"});
    const bool is_finished = staged.outcome == GitOutcome::FINISHED;
    if (!is_finished) { return std::nullopt; }
    const bool has_changes = staged.exit_code != 0;
    const bool is_committed = has_changes && commitRules(session, work);
    const bool is_failed = has_changes && !is_committed;
    if (is_failed) { return std::nullopt; }
    const std::optional<std::string> head = requireGit({"-C", work, "rev-parse", "HEAD"});
    if (!head) { return std::nullopt; }
    return trimLine(head.value());
}

bool commitRules(const Session& session, const std::string& work)
{
    const KiottySpecCheckpointResult latest =
        KiottySpec_readCheckpoint(&session.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const std::string message = formatMessage(
        MessageKey::PUSH_MESSAGE, {findProjectName(session), checkpointName(latest.handle)});
    const std::optional<std::string> committed =
        requireGit({"-C", work, "commit", "--quiet", "-m", message});
    if (!committed) { return false; }
    const std::optional<std::string> pushed =
        requireGit({"-C", work, "push", "--quiet", "origin", "HEAD"});
    return pushed.has_value();
}

std::string findProjectName(const Session& session)
{
    const fs::path root = pathFromUtf8(session.root).lexically_normal();
    const bool has_name = root.has_filename();
    const fs::path name = has_name ? root.filename() : root.parent_path().filename();
    return pathToUtf8(name);
}

std::string findHeadCommit(std::string_view listed)
{
    std::string first_branch {};
    size_t start = 0;
    const size_t length = listed.size();
    while (start < length) {
        const size_t end = std::min(listed.find('\n', start), length);
        const std::string_view line = listed.substr(start, end - start);
        start = end + 1;
        const size_t tab = std::min(line.find('\t'), line.size());
        const std::string name = trimLine(line.substr(std::min(tab + 1, line.size())));
        const std::string commit(line.substr(0, tab));
        if (name == "HEAD") { return commit; }
        const bool is_first_branch = first_branch.empty() && name.rfind(BRANCH_PREFIX, 0) == 0;
        if (is_first_branch) { first_branch = commit; }
    }
    return first_branch;
}

std::optional<std::string> requireGit(const std::vector<std::string>& arguments)
{
    const GitRun run = runGitCommand(arguments);
    const bool is_finished = run.outcome == GitOutcome::FINISHED;
    if (!is_finished) { return std::nullopt; }
    const bool is_success = run.exit_code == 0;
    if (!is_success) {
        const std::string code = std::to_string(run.exit_code);
        failMessage(MessageKey::GIT_FAILED, {describeGitVerb(arguments), code});
        return std::nullopt;
    }
    return run.output;
}

GitRun runGitCommand(const std::vector<std::string>& arguments)
{
    const GitRun run = runGit(arguments, GIT_LIMIT);
    const std::string verb = describeGitVerb(arguments);
    const std::string seconds = std::to_string(GIT_LIMIT.count());
    switch (run.outcome) {
    case GitOutcome::FINISHED: break;
    case GitOutcome::MISSING: failMessage(MessageKey::GIT_MISSING, {}); break;
    case GitOutcome::TIMED_OUT: failMessage(MessageKey::GIT_TIMED_OUT, {verb, seconds}); break;
    case GitOutcome::BROKEN: failMessage(MessageKey::GIT_FAILED, {verb, "-"}); break;
    }
    return run;
}

std::string describeGitVerb(const std::vector<std::string>& arguments)
{
    const bool is_in_directory =
        arguments.size() > DIRECTORY_VERB_POSITION && arguments.front() == "-C";
    const size_t position = is_in_directory ? DIRECTORY_VERB_POSITION : 0;
    return arguments.at(position);
}

}

bool writeText(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    return static_cast<bool>(stream);
}

std::string trimLine(std::string_view text)
{
    const size_t end = text.find_last_not_of(LINE_SPACES);
    if (end == std::string_view::npos) { return std::string(); }
    const size_t begin = text.find_first_not_of(LINE_SPACES);
    return std::string(text.substr(begin, end - begin + 1));
}

}
