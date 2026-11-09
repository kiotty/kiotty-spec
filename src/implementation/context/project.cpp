#include "implementation/context/project.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/schema.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr char PRE_COMMIT_HOOK[] =
    "#!/bin/sh\n"
    "# kiotty-spec: 커밋 전에 DB 를 jsonl 로 내보내고 스테이징한다.\n"
    "ks export jsonl || exit 1\n"
    "git add .kiotty/spec\n";

constexpr char POST_SYNC_HOOK[] =
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

}

Project::Project()
    : _storage(_connection, _failure),
      _projectFiles(_root, _failure),
      _meta(_storage),
      _checkpoints(_storage),
      _history(_storage),
      _items(_storage, _meta),
      _finder(_storage, _items),
      _files(_storage, _meta, _projectFiles),
      _links(_storage, _items, _files),
      _graph(_storage, _links, _items, _finder, _files, _projectFiles),
      _milestoneStore(_storage),
      _milestones(_milestoneStore, _items, _finder, _links, _failure),
      _changes(_storage, _meta),
      _decisions(_storage, _meta, _items),
      _results(_storage, _items, _finder),
      _checker(CheckSources{_items, _finder, _links, _milestones, _results, _decisions, _changes}),
      _context(Stores{_checkpoints, _items, _finder, _files, _links, _graph, _history, _milestones, _changes, _decisions, _results, _checker, _failure}),
      _board(Stores{_checkpoints, _items, _finder, _files, _links, _graph, _history, _milestones, _changes, _decisions, _results, _checker, _failure}),
      _exchange(_storage, _items, _graph, _checker, _failure),
      _journey(JourneySources{_meta, _checkpoints, _history, _finder, _changes, _checker, _failure}),
      _lifecycle(LifecycleSources{_storage, _checkpoints, _history, _items, _links, _checker, _failure}),
      _services(Services{_storage, _meta, _checkpoints, _items, _finder, _files, _links, _graph, _milestones, _changes, _decisions, _results,
                         _checker, _context, _board, _exchange, _journey, _lifecycle, _failure}) {}

SystemPathResult Project::under(Span<const char> base, Span<const char> part) {
    const auto joined = joinPath(base, part);
    if (!joined) { return SystemPathResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("경로가 너무 김: "), base})); }
    return joined;
}

KiottySpecStatus Project::writeText(Span<const char> path, Span<const char> content) {
    if (writeWholeFile(path, content) == KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), path});
}

KiottySpecStatus Project::prepareRoot(std::string_view root) {
    const auto given = under(spanOf(root), Span<const char>());
    if (!given) { return given.code; }
    if (!isDirectory(spanOf(root))) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("프로젝트 루트가 디렉토리가 아님: "), given.value.view()});
    }
    const auto absolute = absolutePath(spanOf(root));
    if (!absolute) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("프로젝트 루트를 절대 경로로 바꿀 수 없음: "), given.value.view()});
    }
    const auto spec = under(absolute.value.view(), literalText(SPEC_DIRECTORY));
    if (!spec) { return spec.code; }
    if (createDirectories(spec.value.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kiotty 디렉토리를 만들 수 없음")});
    }
    _root = absolute.value;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Project::openDatabase() {
    const auto database = under(_root.view(), literalText(DATABASE_FILE));
    if (!database) { return database.code; }
    const KiottySpecStatus opened = _connection.open(database.value.view());
    if (opened == KIOTTY_SPEC_STATUS_INPUT_OUTPUT || opened == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT,
                              {literalText("cannot open database "), database.value.view(), literalText(": "), _connection.message()});
    }
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(opened, {literalText("exec failed: "), _connection.message()}); }
    return ensure(_storage);
}

KiottySpecStatus Project::open(std::string_view root) {
    if (_opened) { return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("프로젝트가 이미 열려 있음")}); }
    const KiottySpecStatus prepared = prepareRoot(root);
    if (prepared != KIOTTY_SPEC_STATUS_SUCCESS) { return prepared; }
    const KiottySpecStatus opened = openDatabase();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    _opened = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

SystemPathResult Project::jsonlDir() { return under(_root.view(), literalText(JSONL_DIRECTORY)); }

KiottySpecStatus Project::updateGitignore() {
    const auto gitignore = under(_root.view(), literalText(GITIGNORE_FILE));
    if (!gitignore) { return gitignore.code; }
    std::string content = readWholeFile(gitignore.value.view()).value_or("");
    const std::string_view entries[] = {".kiotty/spec.db", ".kiotty/spec.db-wal", ".kiotty/spec.db-shm"};
    for (const auto entry : entries) {
        if (content.find(entry) != std::string::npos) { continue; }
        if (!content.empty() && content.back() != '\n') { content += "\n"; }
        content += std::string(entry) + "\n";
    }
    return writeText(gitignore.value.view(), spanOf(content));
}

KiottySpecStatus Project::init(std::string_view projectName, bool installGitHooks) {
    const std::string name = projectName.empty() ? std::string(viewOf(fileNameOf(_root.view()))) : std::string(projectName);
    KiottySpecStatus status = _meta.set("project.name", name);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = _meta.set("project.created_at", std::to_string(nowEpochSeconds()));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = updateGitignore();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || !installGitHooks) { return status; }
    return installHooks();
}

KiottySpecStatus Project::installHooks() {
    const auto git = under(_root.view(), literalText(GIT_DIRECTORY));
    if (!git) { return git.code; }
    if (!isDirectory(git.value.view())) {
        return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("git 저장소가 아님 (.git 없음): "), _root.view()});
    }
    const auto hooks = under(_root.view(), literalText(HOOKS_DIRECTORY));
    if (!hooks) { return hooks.code; }
    createDirectories(hooks.value.view());
    const std::pair<Span<const char>, Span<const char>> scripts[] = {{literalText("pre-commit"), literalText(PRE_COMMIT_HOOK)},
                                                                     {literalText("post-merge"), literalText(POST_SYNC_HOOK)},
                                                                     {literalText("post-checkout"), literalText(POST_SYNC_HOOK)}};
    for (const auto &[name, script] : scripts) {
        const auto hook = under(hooks.value.view(), name);
        if (!hook) { return hook.code; }
        const KiottySpecStatus written = writeText(hook.value.view(), script);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
        makeExecutable(hook.value.view());
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Project::transaction(TransactionStep step) {
    switch (step) {
    case TransactionStep::BEGIN: return _storage.execute(literalText("BEGIN IMMEDIATE"));
    case TransactionStep::COMMIT: return _storage.execute(literalText("COMMIT"));
    case TransactionStep::ROLLBACK: return _storage.execute(literalText("ROLLBACK"));
    }
    return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 트랜잭션 단계")});
}

}
