#include "implementation/context/context.h"

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
    "git add -A .kiotty/spec\n";

constexpr char POST_SYNC_HOOK[] =
    "#!/bin/sh\n"
    "# kiotty-spec: 병합 · 체크아웃 뒤 jsonl 로 DB 를 다시 만든다.\n"
    "[ -d .kiotty/spec ] && ks import jsonl\n"
    "exit 0\n";

constexpr char SPEC_DIRECTORY[] = ".kiotty";
constexpr char JSONL_DIRECTORY[] = ".kiotty/spec";
constexpr char DATABASE_FILE[] = ".kiotty/spec.db";
constexpr char LEGACY_SUFFIX[] = ".v1";
constexpr char GITIGNORE_FILE[] = ".gitignore";
constexpr char GIT_DIRECTORY[] = ".git";
constexpr char HOOKS_DIRECTORY[] = ".git/hooks";

}

Context::Context(const Configuration& configuration)
    : _storage(_connection, _failure),
      _meta(_storage),
      _checkpoints(_storage, configuration.maxCheckpoints),
      _items(_storage, configuration.maxItems, configuration.maxTags),
      _relations(_storage, _items, configuration.maxRelations),
      _decisions(_storage),
      _workspace(Workspace{_storage, _failure, _catalog, _meta, _checkpoints, _items, _relations, _decisions}),
      _checker(_workspace),
      _lifecycle(_workspace, _checker),
      _editor(_workspace, _lifecycle),
      _exchange(_workspace),
      _cursors(configuration.maxCursors, configuration.maxCursorRows),
      _finder(_workspace, _cursors, _checker, configuration.maxItems),
      _work(configuration.workBufferBytes),
      _differ(_workspace, _cursors, _work)
{
}

SystemPathResult Context::under(Span<const char> base, Span<const char> part)
{
    const auto joined = joinPath(base, part);
    if (!joined) { return SystemPathResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("경로가 너무 김: "), base})); }
    return joined;
}

KiottySpecStatus Context::open(Span<const char> root)
{
    if (_opened) { return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("프로젝트가 이미 열려 있음")}); }
    if (!isDirectory(root)) { return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("프로젝트 루트가 디렉토리가 아님: "), root}); }
    const auto absolute = absolutePath(root);
    if (!absolute) { return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("프로젝트 루트를 절대 경로로 바꿀 수 없음: "), root}); }
    _root = absolute.value;
    const auto spec = under(_root.view(), literalText(SPEC_DIRECTORY));
    if (!spec) { return spec.code; }
    if (createDirectories(spec.value.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kiotty 디렉토리를 만들 수 없음")});
    }
    const KiottySpecStatus catalog = _catalog.load();
    if (catalog != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(catalog, {literalText("모델 표를 적재할 수 없음")}); }
    const KiottySpecStatus opened = openDatabase();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    const KiottySpecStatus loaded = _workspace.reload();
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    _opened = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::openDatabase()
{
    const auto database = under(_root.view(), literalText(DATABASE_FILE));
    if (!database) { return database.code; }
    for (int attempt = 0; attempt < 2; ++attempt) {
        const KiottySpecStatus opened = _connection.open(database.value.view());
        if (opened != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("DB 를 열 수 없음 "), database.value.view(), literalText(": "), _connection.message()});
        }
        const auto version = ensure(_storage);
        if (!version) { return version.code; }
        if (version.value == SCHEMA_VERSION) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        _connection.close();
        const KiottySpecStatus moved = moveLegacyDatabase(database.value.view());
        if (moved != KIOTTY_SPEC_STATUS_SUCCESS) { return moved; }
    }
    return _failure.raise(KIOTTY_SPEC_STATUS_DATABASE, {literalText("DB 스키마를 만들 수 없음")});
}

KiottySpecStatus Context::moveLegacyDatabase(Span<const char> database)
{
    SystemPath target;
    if (target.assign(database) != KIOTTY_SPEC_STATUS_SUCCESS || target.append(literalText(LEGACY_SUFFIX)) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("경로가 너무 김: "), database});
    }
    removeFile(target.view());
    if (renameFile(database, target.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("옛 스키마 DB 를 옮길 수 없음: "), database});
    }
    _moved_legacy = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

SystemPathResult Context::jsonlDirectory() { return under(_root.view(), literalText(JSONL_DIRECTORY)); }

KiottySpecStatus Context::updateGitignore()
{
    const auto gitignore = under(_root.view(), literalText(GITIGNORE_FILE));
    if (!gitignore) { return gitignore.code; }
    std::string content = readWholeFile(gitignore.value.view()).value_or("");
    const std::string_view entries[] = {".kiotty/spec.db", ".kiotty/spec.db-wal", ".kiotty/spec.db-shm", ".kiotty/spec.db.v1"};
    for (const auto entry : entries) {
        if (content.find(entry) != std::string::npos) { continue; }
        if (!content.empty() && content.back() != '\n') { content += "\n"; }
        content += std::string(entry) + "\n";
    }
    if (writeWholeFile(gitignore.value.view(), spanOf(content)) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), gitignore.value.view()});
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::installHooks()
{
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
    for (const auto& [name, script] : scripts) {
        const auto hook = under(hooks.value.view(), name);
        if (!hook) { return hook.code; }
        if (writeWholeFile(hook.value.view(), script) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), hook.value.view()});
        }
        makeExecutable(hook.value.view());
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::init(Span<const char> name, bool install_hooks)
{
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_root.view());
    KiottySpecStatus status = _meta.set(literalText("project.name"), project);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    const std::string now = std::to_string(nowEpochSeconds());
    status = _meta.set(literalText("project.created_at"), spanOf(now));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = updateGitignore();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || !install_hooks) { return status; }
    return installHooks();
}

}
