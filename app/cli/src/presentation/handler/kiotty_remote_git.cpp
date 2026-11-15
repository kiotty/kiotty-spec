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

constexpr std::string_view CLONE_DIRECTORY_NAME = "rule";
constexpr std::string_view IGNORED_ENTRY = "rule/";
constexpr std::string_view BRANCH_PREFIX = "refs/heads/";
constexpr std::string_view LINE_SPACES = " \t\r\n";
constexpr std::chrono::seconds GIT_LIMIT {300};
constexpr size_t DIRECTORY_VERB_POSITION = 2;

bool ensureIgnored(const RemotePaths& paths);
bool hasLine(std::string_view text, std::string_view wanted);
std::optional<std::string> readCloneOrigin(const RemotePaths& paths);
bool hasClone(const RemotePaths& paths);
std::optional<std::string> exportAndPush(const Session& session, const RemotePaths& paths);
std::optional<std::string> commitAndPush(const Session& session, const RemotePaths& paths);
bool commitRules(const Session& session, const std::string& clone);
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
    paths.clone = paths.kiotty / CLONE_DIRECTORY_NAME;
    paths.rules = paths.clone / RULE_FILE_NAME;
    return paths;
}

std::optional<std::string> readCloneHead(const RemotePaths& paths)
{
    const bool has_clone = hasClone(paths);
    if (!has_clone) { return std::string(); }
    const std::string clone = pathToUtf8(paths.clone);
    const GitRun head =
        runGitCommand({"-C", clone, "rev-parse", "--verify", "--quiet", "HEAD"});
    const bool is_finished = head.outcome == GitOutcome::FINISHED;
    if (!is_finished) { return std::nullopt; }
    const bool has_head = head.exit_code == 0;
    if (!has_head) { return std::string(); }
    return trimLine(head.output);
}

std::optional<std::string> publishRules(const Session& session, const KiottySpecRemote& remote,
                                        const RemotePaths& paths)
{
    const std::optional<std::string> current = readRemoteCommit(remote.url);
    if (!current) { return std::nullopt; }
    const std::string& head = current.value();
    const bool has_head = head.size() > 0;
    const bool is_moved = has_head && head != remote.rule_base;
    if (is_moved) {
        failMessage(MessageKey::REMOTE_CHANGED, {head});
        return std::nullopt;
    }
    const bool is_cloned = ensureClone(paths, remote.url);
    const bool is_ready = is_cloned && (!has_head || moveClone(paths, head));
    if (!is_ready) { return std::nullopt; }
    const std::optional<std::string> pushed = exportAndPush(session, paths);
    if (!pushed) { removeClone(paths); }
    return pushed;
}

std::optional<std::string> readRemoteCommit(const std::string& url)
{
    const std::optional<std::string> listed = requireGit({"ls-remote", url});
    if (!listed) { return std::nullopt; }
    return findHeadCommit(listed.value());
}

bool ensureClone(const RemotePaths& paths, const std::string& url)
{
    const bool is_ignored = ensureIgnored(paths);
    if (!is_ignored) { return false; }
    const std::optional<std::string> origin = readCloneOrigin(paths);
    const bool is_same = origin.has_value() && origin.value() == url;
    if (is_same) { return true; }
    const bool is_removed = removeClone(paths);
    if (!is_removed) { return false; }
    const std::string clone = pathToUtf8(paths.clone);
    const std::optional<std::string> cloned =
        requireGit({"clone", "--quiet", "--config", "core.autocrlf=false", url, clone});
    return cloned.has_value();
}

bool moveClone(const RemotePaths& paths, const std::string& commit)
{
    const std::string clone = pathToUtf8(paths.clone);
    const std::optional<std::string> fetched =
        requireGit({"-C", clone, "fetch", "--quiet", "origin"});
    if (!fetched) { return false; }
    return resetClone(paths, commit);
}

bool resetClone(const RemotePaths& paths, const std::string& commit)
{
    const std::string clone = pathToUtf8(paths.clone);
    const std::optional<std::string> reset =
        requireGit({"-C", clone, "reset", "--hard", "--quiet", commit});
    return reset.has_value();
}

bool removeClone(const RemotePaths& paths)
{
    std::error_code error {};
    fs::remove_all(paths.clone, error);
    if (error) { failMessage(MessageKey::REMOTE_FILE_UNWRITABLE, {pathToUtf8(paths.clone)}); }
    const bool is_removed = !error;
    return is_removed;
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

std::optional<std::string> readCloneOrigin(const RemotePaths& paths)
{
    const bool has_clone = hasClone(paths);
    if (!has_clone) { return std::nullopt; }
    const std::string clone = pathToUtf8(paths.clone);
    const GitRun origin = runGit({"-C", clone, "config", "--get", "remote.origin.url"}, GIT_LIMIT);
    const bool is_read = origin.outcome == GitOutcome::FINISHED && origin.exit_code == 0;
    if (!is_read) { return std::nullopt; }
    return trimLine(origin.output);
}

bool hasClone(const RemotePaths& paths)
{
    std::error_code error {};
    const bool is_directory = fs::is_directory(paths.clone / ".git", error);
    return is_directory;
}

std::optional<std::string> exportAndPush(const Session& session, const RemotePaths& paths)
{
    const std::string exported = pathToUtf8(paths.rules);
    const KiottySpecStatus written =
        KiottySpec_exportRules(&session.context, textOf(exported)).code;
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, written);
        return std::nullopt;
    }
    return commitAndPush(session, paths);
}

std::optional<std::string> commitAndPush(const Session& session, const RemotePaths& paths)
{
    const std::string clone = pathToUtf8(paths.clone);
    const std::optional<std::string> added =
        requireGit({"-C", clone, "add", std::string(RULE_FILE_NAME)});
    if (!added) { return std::nullopt; }
    const GitRun staged = runGitCommand({"-C", clone, "diff", "--cached", "--quiet"});
    const bool is_finished = staged.outcome == GitOutcome::FINISHED;
    if (!is_finished) { return std::nullopt; }
    const bool has_changes = staged.exit_code != 0;
    const bool is_committed = has_changes && commitRules(session, clone);
    const bool is_failed = has_changes && !is_committed;
    if (is_failed) { return std::nullopt; }
    const std::optional<std::string> head = requireGit({"-C", clone, "rev-parse", "HEAD"});
    if (!head) { return std::nullopt; }
    return trimLine(head.value());
}

bool commitRules(const Session& session, const std::string& clone)
{
    const KiottySpecCheckpointResult latest =
        KiottySpec_readCheckpoint(&session.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const std::string message = formatMessage(
        MessageKey::PUSH_MESSAGE, {findProjectName(session), checkpointName(latest.handle)});
    const std::optional<std::string> committed =
        requireGit({"-C", clone, "commit", "--quiet", "-m", message});
    if (!committed) { return false; }
    const std::optional<std::string> pushed =
        requireGit({"-C", clone, "push", "--quiet", "origin", "HEAD"});
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
