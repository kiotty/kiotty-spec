#include "implementation/context/project.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/data/datasource/schema.h"

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kPreCommit =
    "#!/bin/sh\n"
    "# kiotty-spec: 커밋 전에 DB 를 jsonl 로 내보내고 스테이징한다.\n"
    "ks export jsonl || exit 1\n"
    "git add .kiotty/spec\n";

constexpr std::string_view kPostSync =
    "#!/bin/sh\n"
    "# kiotty-spec: 병합·체크아웃 뒤 jsonl 로 DB 를 다시 만든다.\n"
    "[ -d .kiotty/spec ] && ks import jsonl\n"
    "exit 0\n";

constexpr char SPEC_DIRECTORY[] = ".kiotty";
constexpr char JSONL_DIRECTORY[] = ".kiotty/spec";
constexpr char DATABASE_FILE[] = ".kiotty/spec.db";
constexpr char GITIGNORE_FILE[] = ".gitignore";
constexpr char GIT_DIRECTORY[] = ".git";
constexpr char HOOKS_DIRECTORY[] = ".git/hooks";

void requireUser(KiottySpecActor actor, std::string_view what) {
    require(actor == KIOTTY_SPEC_ACTOR_USER, KIOTTY_SPEC_STATUS_PERMISSION, std::string(what) + ": 사용자만 할 수 있음 (R-2)");
}

std::string textOf(Span<const char> text) { return std::string(viewOf(text)); }

SystemPath under(Span<const char> root, Span<const char> part) {
    const auto joined = joinPath(root, part);
    require(static_cast<bool>(joined), KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "경로가 너무 김: " + textOf(root));
    return joined.value;
}

void writeText(Span<const char> path, std::string_view content) {
    require(writeWholeFile(path, spanOf(content)) == KIOTTY_SPEC_STATUS_SUCCESS, KIOTTY_SPEC_STATUS_INPUT_OUTPUT,
            "파일을 쓸 수 없음: " + textOf(path));
}

}

SystemPath Project::prepareRoot(std::string_view root) {
    const SystemPath given = under(spanOf(root), Span<const char>());
    require(isDirectory(spanOf(root)), KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "프로젝트 루트가 디렉토리가 아님: " + textOf(given.view()));
    const auto absolute = absolutePath(spanOf(root));
    require(static_cast<bool>(absolute), KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "프로젝트 루트를 절대 경로로 바꿀 수 없음: " + textOf(given.view()));
    const SystemPath spec = under(absolute.value.view(), literalText(SPEC_DIRECTORY));
    require(createDirectories(spec.view()) == KIOTTY_SPEC_STATUS_SUCCESS, KIOTTY_SPEC_STATUS_INPUT_OUTPUT, ".kiotty 디렉토리를 만들 수 없음");
    return absolute.value;
}

Project::Project(std::string_view root)
    : root_(prepareRoot(root)),
      storage_(connection_, failure_),
      projectFiles_(root_, failure_),
      meta(storage_),
      checkpoints(storage_),
      history(storage_),
      items(storage_, meta),
      finder(storage_, items),
      files(storage_, meta, projectFiles_),
      links(storage_, items, files),
      graph(storage_, links, items, finder, files, projectFiles_),
      milestoneStore(storage_),
      milestones(milestoneStore, items, finder, links, failure_),
      changes(storage_, meta),
      decisions(storage_, meta, items),
      results(storage_, items, finder),
      checker(CheckSources{items, finder, links, milestones, results, decisions, changes}),
      context(Stores{checkpoints, items, finder, files, links, graph, history, milestones, changes, decisions, results, checker, failure_}),
      exchange(storage_, items, graph, checker, failure_),
      journey(JourneySources{meta, checkpoints, history, finder, changes, checker, failure_}) {
    open();
}

SystemPath Project::jsonlDir() const { return under(root_.view(), literalText(JSONL_DIRECTORY)); }

void Project::open() {
    const SystemPath database = under(root_.view(), literalText(DATABASE_FILE));
    const KiottySpecStatus opened = connection_.open(database.view());
    const bool cannotOpen = opened == KIOTTY_SPEC_STATUS_INPUT_OUTPUT || opened == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    if (cannotOpen) { fail(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "cannot open database " + textOf(database.view()) + ": " + textOf(connection_.message())); }
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { fail(opened, "exec failed: " + textOf(connection_.message())); }
    expect(ensure(storage_), failure_);
}

void Project::init(std::string_view projectName, bool installGitHooks) {
    const std::string name = projectName.empty() ? textOf(fileNameOf(root_.view())) : std::string(projectName);
    expect(meta.set("project.name", name), failure_);
    expect(meta.set("project.created_at", std::to_string(nowEpochSeconds())), failure_);

    const SystemPath gitignore = under(root_.view(), literalText(GITIGNORE_FILE));
    std::string content = readWholeFile(gitignore.view()).value_or("");
    const std::string_view entries[] = {".kiotty/spec.db", ".kiotty/spec.db-wal", ".kiotty/spec.db-shm"};
    for (const auto entry : entries) {
        if (content.find(entry) != std::string::npos) { continue; }
        if (!content.empty() && content.back() != '\n') { content += "\n"; }
        content += std::string(entry) + "\n";
    }
    writeText(gitignore.view(), content);

    if (installGitHooks) { installHooks(); }
}

void Project::installHooks() {
    const SystemPath git = under(root_.view(), literalText(GIT_DIRECTORY));
    require(isDirectory(git.view()), KIOTTY_SPEC_STATUS_PRECONDITION, "git 저장소가 아님 (.git 없음): " + textOf(root_.view()));
    const SystemPath hooks = under(root_.view(), literalText(HOOKS_DIRECTORY));
    createDirectories(hooks.view());
    const std::pair<const char *, std::string_view> scripts[] = {
        {"pre-commit", kPreCommit}, {"post-merge", kPostSync}, {"post-checkout", kPostSync}};
    for (const auto &[name, script] : scripts) {
        const SystemPath hook = under(hooks.view(), boundedText(name, MAX_SYSTEM_PATH_LENGTH));
        writeText(hook.view(), script);
        makeExecutable(hook.view());
    }
}

int64_t Project::confirm(KiottySpecActor actor, std::string_view cpId) {
    requireUser(actor, "체크포인트 확정");
    const int64_t seq = expect(checkpoints.resolve(cpId), failure_);
    require(seq != kPending, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "PENDING 은 확정할 수 없음. 먼저 체크포인트를 찍을 것");
    const auto record = expect(checkpoints.get(seq), failure_);
    if (record.confirmed) {
        return seq;
    }
    const auto verdict = expect(checker.confirmCheck(record.step, seq), failure_);
    require(verdict.ok, KIOTTY_SPEC_STATUS_PRECONDITION, "Step " + std::to_string(record.step) + " 확정 조건 미충족:\n" + verdict.report);
    expect(checkpoints.markConfirmed(seq), failure_);
    return seq;
}

int64_t Project::release(KiottySpecActor actor, std::string_view note) {
    requireUser(actor, "RELEASE");
    const auto verdict = expect(checker.releaseCheck(), failure_);
    require(verdict.ok, KIOTTY_SPEC_STATUS_PRECONDITION, "RELEASE 조건 미충족 (R-10):\n" + verdict.report);
    Savepoint savepoint(connection_);
    expect(storage_.check(savepoint.begin()), failure_);
    const int64_t seq = expect(checkpoints.create(10, note, KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE), failure_);
    expect(checkpoints.markConfirmed(seq), failure_);
    expect(storage_.check(savepoint.commit()), failure_);
    return seq;
}

size_t Project::prune(KiottySpecActor actor, std::string_view baseCpId) {
    requireUser(actor, "체크포인트 정리");
    const int64_t base = expect(checkpoints.resolve(baseCpId), failure_);
    require(base != kPending, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "PENDING 은 기준이 될 수 없음");
    require(expect(checkpoints.get(base), failure_).confirmed, KIOTTY_SPEC_STATUS_PRECONDITION, "기준 체크포인트는 확정된 것이어야 함");
    for (const auto &record : expect(checkpoints.list(0), failure_)) {
        require(!(record.seq < base && record.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE), KIOTTY_SPEC_STATUS_PRECONDITION,
                "RELEASE 체크포인트 " + checkpointName(record.seq) + " 는 지울 수 없음. 그 이후를 기준으로 할 것");
    }
    return expect(history.prune(base), failure_);
}

void Project::removeItem(std::string_view itemId) {
    Savepoint savepoint(connection_);
    expect(storage_.check(savepoint.begin()), failure_);
    expect(items.get(itemId), failure_);
    expect(links.removeAllFor(itemId), failure_);
    expect(items.remove(itemId), failure_);
    expect(storage_.check(savepoint.commit()), failure_);
}

void Project::beginTransaction() { expect(storage_.execute(literalText("BEGIN IMMEDIATE")), failure_); }
void Project::commitTransaction() { expect(storage_.execute(literalText("COMMIT")), failure_); }
void Project::rollbackTransaction() { expect(storage_.execute(literalText("ROLLBACK")), failure_); }

}
