#include "presentation/commands/kiotty_remote.h"

#include "infra/kiotty_path_utf8.h"

#include <system_error>

namespace kiotty::cli {

const std::vector<std::string_view> REMOTE_FLAGS = {"body"};

namespace {

namespace fs = std::filesystem;

constexpr std::string_view REMOTE_KIND_NAME = "rule";
constexpr size_t KIND_POSITION = 0;
constexpr size_t ID_POSITION = 1;

std::optional<KiottySpecItemKind> readRemoteKind(const Session& session, std::string_view verb);
std::optional<KiottySpecRemote> readRemoteState(const Session& session);
std::optional<KiottySpecRemote> requireRemote(const Session& session);
std::optional<KiottySpecRemote> makeRemote(std::string_view url, std::string_view base);
int fetchClone(const RemotePaths& paths, const std::string& url);
int runWithPulledRules(const Session& session, RemoteRulesCommand command);
int runWithRemoteRules(const Session& session, const RemotePaths& paths,
                       RemoteRulesCommand command);
int printPullCounts(const Session& local, const Session& remote);
int listRules(const Session& local, const Session& remote);
int printRemoteDiff(const Session& local, const Session& remote);
int printRemoteBody(const Session& local, const Session& remote);
int acceptPulledRules(const Session& local, const Session& remote);
int dropPulledCommit(const RemotePaths& paths, const KiottySpecRemote& remote);

}

int openRemote(const Session& session)
{
    const std::string url = readPositional(session, KIND_POSITION).value_or("");
    const bool has_url = !url.empty();
    if (!has_url) { return usage(findMessage(MessageKey::REMOTE_OPEN_USAGE)); }
    const std::optional<KiottySpecRemote> current = readRemoteState(session);
    if (!current) { return 1; }
    const KiottySpecRemote& previous = current.value();
    const bool is_same_url = url == std::string_view(previous.url);
    const std::string_view base = is_same_url ? previous.rule_base : "";
    const std::optional<KiottySpecRemote> remote = makeRemote(url, base);
    if (!remote) { return failMessage(MessageKey::REMOTE_INVALID, {}); }
    const KiottySpecStatus written = KiottySpec_writeRemote(&session.context, &remote.value()).code;
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written); }
    const bool is_cloned = ensureClone(findRemotePaths(session), url);
    return is_cloned ? 0 : 1;
}

int closeRemote(const Session& session)
{
    const KiottySpecRemote cleared {};
    const KiottySpecStatus written = KiottySpec_writeRemote(&session.context, &cleared).code;
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written); }
    const bool is_removed = removeClone(findRemotePaths(session));
    return is_removed ? 0 : 1;
}

int pushRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "push");
    if (!kind) { return USAGE_EXIT_CODE; }
    const bool is_user = session.actor == KIOTTY_SPEC_ACTOR_USER;
    if (!is_user) { return failMessage(MessageKey::USER_ONLY, {}); }
    const std::optional<KiottySpecRemote> remote = requireRemote(session);
    if (!remote) { return 1; }
    const RemotePaths paths = findRemotePaths(session);
    const std::optional<std::string> commit = publishRules(session, remote.value(), paths);
    if (!commit) { return 1; }
    const std::optional<KiottySpecRemote> updated = makeRemote(remote.value().url, commit.value());
    if (!updated) { return failMessage(MessageKey::REMOTE_INVALID, {}); }
    const KiottySpecStatus saved = KiottySpec_writeRemote(&session.context, &updated.value()).code;
    if (saved != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, saved); }
    printRow({commit.value()});
    return 0;
}

int checkRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "check");
    if (!kind) { return USAGE_EXIT_CODE; }
    const std::optional<KiottySpecRemote> remote = requireRemote(session);
    if (!remote) { return 1; }
    const std::optional<std::string> current = readRemoteCommit(remote.value().url);
    if (!current) { return 1; }
    const bool is_same = current.value() == std::string_view(remote.value().rule_base);
    if (is_same) {
        printRow({"same"});
        return 0;
    }
    printRow({"changed", cell(current.value())});
    return 0;
}

int fetchRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "pull");
    if (!kind) { return USAGE_EXIT_CODE; }
    const std::optional<KiottySpecRemote> remote = requireRemote(session);
    if (!remote) { return 1; }
    const RemotePaths paths = findRemotePaths(session);
    const int pulled = fetchClone(paths, remote.value().url);
    if (pulled != 0) { return pulled; }
    return runWithRemoteRules(session, paths, printPullCounts);
}

int listRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "list");
    if (!kind) { return USAGE_EXIT_CODE; }
    return runWithPulledRules(session, listRules);
}

int diffRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "diff");
    if (!kind) { return USAGE_EXIT_CODE; }
    return runWithPulledRules(session, printRemoteDiff);
}

int showRemoteRuleBody(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "body");
    if (!kind) { return USAGE_EXIT_CODE; }
    const std::optional<std::string> id = readPositional(session, ID_POSITION);
    if (!id) { return usage(findMessage(MessageKey::REMOTE_BODY_USAGE)); }
    return runWithPulledRules(session, printRemoteBody);
}

int acceptRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "accept");
    if (!kind) { return USAGE_EXIT_CODE; }
    return runWithPulledRules(session, acceptPulledRules);
}

int dropRemoteRules(const Session& session)
{
    const std::optional<KiottySpecItemKind> kind = readRemoteKind(session, "discard");
    if (!kind) { return USAGE_EXIT_CODE; }
    const bool is_user = session.actor == KIOTTY_SPEC_ACTOR_USER;
    if (!is_user) { return failMessage(MessageKey::USER_ONLY, {}); }
    const std::optional<KiottySpecRemote> remote = requireRemote(session);
    if (!remote) { return 1; }
    return dropPulledCommit(findRemotePaths(session), remote.value());
}

namespace {

std::optional<KiottySpecItemKind> readRemoteKind(const Session& session, std::string_view verb)
{
    const std::string text = readPositional(session, KIND_POSITION).value_or("");
    const std::optional<KiottySpecItemKind> kind = parseRemoteKind(text);
    if (!kind) { usage(formatMessage(MessageKey::REMOTE_KIND_USAGE, {verb})); }
    return kind;
}

std::optional<KiottySpecRemote> makeRemote(std::string_view url, std::string_view base)
{
    KiottySpecRemote remote {};
    const bool is_fit = url.size() < sizeof(remote.url) && base.size() < sizeof(remote.rule_base);
    if (!is_fit) { return std::nullopt; }
    url.copy(remote.url, url.size());
    base.copy(remote.rule_base, base.size());
    return remote;
}

int fetchClone(const RemotePaths& paths, const std::string& url)
{
    const std::optional<std::string> commit = readRemoteCommit(url);
    if (!commit) { return 1; }
    const bool has_commit = commit.value().size() > 0;
    if (!has_commit) { return failMessage(MessageKey::REMOTE_RULE_FILE_MISSING, {}); }
    const bool is_cloned = ensureClone(paths, url);
    const bool is_moved = is_cloned && moveClone(paths, commit.value());
    if (!is_moved) { return 1; }
    std::error_code error {};
    const bool has_rules = fs::is_regular_file(paths.rules, error);
    if (!has_rules) { return failMessage(MessageKey::REMOTE_RULE_FILE_MISSING, {}); }
    return 0;
}

int runWithPulledRules(const Session& session, RemoteRulesCommand command)
{
    const std::optional<KiottySpecRemote> remote = requireRemote(session);
    if (!remote) { return 1; }
    const RemotePaths paths = findRemotePaths(session);
    const std::optional<std::string> head = readCloneHead(paths);
    if (!head) { return 1; }
    const std::string& commit = head.value();
    const bool is_pulled = commit.size() > 0 && commit != remote.value().rule_base;
    if (!is_pulled) { return failMessage(MessageKey::REMOTE_RULES_MISSING, {}); }
    return runWithRemoteRules(session, paths, command);
}

std::optional<KiottySpecRemote> requireRemote(const Session& session)
{
    const std::optional<KiottySpecRemote> remote = readRemoteState(session);
    if (!remote) { return std::nullopt; }
    const bool has_url = std::string_view(remote.value().url).size() > 0;
    if (!has_url) {
        failMessage(MessageKey::REMOTE_NOT_OPENED, {});
        return std::nullopt;
    }
    return remote;
}

int runWithRemoteRules(const Session& session, const RemotePaths& paths,
                       RemoteRulesCommand command)
{
    std::error_code error {};
    const bool has_rules = fs::is_regular_file(paths.rules, error);
    if (!has_rules) { return failMessage(MessageKey::REMOTE_RULES_MISSING, {}); }
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (created.code != KIOTTY_SPEC_STATUS_SUCCESS) { return failWithoutContext(created.code); }
    const Session remote {*created.value, session.actor, session.arguments, session.root};
    const std::string file = pathToUtf8(paths.rules);
    const KiottySpecStatus opened = KiottySpec_open(&remote.context, textOf(file)).code;
    const bool is_opened = opened == KIOTTY_SPEC_STATUS_SUCCESS;
    const int code = is_opened ? command(session, remote) : fail(remote, opened);
    KiottySpec_releaseContext(&remote.context);
    return code;
}

int printPullCounts(const Session& local, const Session& remote)
{
    const std::optional<std::string> commit = readCloneHead(findRemotePaths(local));
    const std::optional<std::vector<RuleDiffRow>> rows = readRuleDiffs(local, remote);
    const bool is_read = commit.has_value() && rows.has_value();
    if (!is_read) { return 1; }
    const DiffCounts counts = countDiffs(rows.value());
    printRow({formatShortCommit(commit.value()), "added " + std::to_string(counts.added),
              "removed " + std::to_string(counts.removed),
              "modified " + std::to_string(counts.modified)});
    return 0;
}

int listRules(const Session&, const Session& remote)
{
    const KiottySpecCursorResult cursor =
        KiottySpec_openItemCursor(&remote.context, KIOTTY_SPEC_ITEM_KIND_RULE, nullptr, 0);
    return printItemCursor(remote, cursor, ColumnPlan{});
}

int printRemoteDiff(const Session& local, const Session& remote)
{
    const std::optional<std::vector<RuleDiffRow>> rows = readRuleDiffs(local, remote);
    if (!rows) { return 1; }
    const std::vector<std::string>& positional = local.arguments.positional;
    const std::vector<std::string> wanted(positional.begin() + ID_POSITION, positional.end());
    return printRuleDiffs(local, remote, filterRuleDiffs(rows.value(), wanted));
}

int printRemoteBody(const Session& local, const Session& remote)
{
    const std::string id = readPositional(local, ID_POSITION).value_or("");
    return printItemBody(remote, id);
}

int acceptPulledRules(const Session& local, const Session& remote)
{
    const std::optional<KiottySpecRemote> state = readRemoteState(local);
    const std::optional<std::string> commit = readCloneHead(findRemotePaths(local));
    const std::optional<std::vector<RuleDiffRow>> rows = readRuleDiffs(local, remote);
    const bool is_ready = state.has_value() && commit.has_value() && rows.has_value();
    if (!is_ready) { return 1; }
    const AcceptImpact impact = collectImpact(local, rows.value());
    const AcceptTexts texts = buildAcceptTexts(state.value(), commit.value(), rows.value(), impact);
    KiottySpecAcceptInput input {};
    input.description = textOf(texts.description);
    input.title = textOf(texts.title);
    input.summary = textOf(texts.summary);
    input.body = textOf(texts.body);
    input.rule_base = textOf(texts.rule_base);
    const KiottySpecAcceptResult accepted =
        KiottySpec_acceptRules(&local.context, &remote.context, &input, local.actor);
    if (accepted.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(local, accepted.code); }
    printAcceptResult(accepted, impact);
    return 0;
}

int dropPulledCommit(const RemotePaths& paths, const KiottySpecRemote& remote)
{
    const std::optional<std::string> head = readCloneHead(paths);
    if (!head) { return 1; }
    const std::string base(remote.rule_base);
    const bool is_pulled = head.value().size() > 0 && head.value() != base;
    if (!is_pulled) { return 0; }
    const bool has_base = base.size() > 0;
    const bool is_dropped = has_base ? resetClone(paths, base) : removeClone(paths);
    return is_dropped ? 0 : 1;
}

std::optional<KiottySpecRemote> readRemoteState(const Session& session)
{
    const KiottySpecRemoteResult read = KiottySpec_readRemote(&session.context);
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        fail(session, read.code);
        return std::nullopt;
    }
    return read.value;
}

}

std::optional<KiottySpecItemKind> parseRemoteKind(std::string_view text)
{
    const bool is_rule = text == REMOTE_KIND_NAME;
    if (!is_rule) { return std::nullopt; }
    return KIOTTY_SPEC_ITEM_KIND_RULE;
}

}
