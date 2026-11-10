#include "implementation/context/kiotty_context.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view DATABASE_EXTENSION = ".kisp";
constexpr char BODY_SUFFIX[] = ".body";
constexpr size_t MAX_EPOCH_DIGITS = 20;

ItemCapacity itemCapacity(const Configuration& configuration)
{
    return ItemCapacity{configuration.maxItems, configuration.maxTags, configuration.maxItems * VERSION_FACTOR, configuration.maxTags * VERSION_FACTOR};
}

}

Context::Context(const Configuration& configuration)
    : _work(configuration.workBufferBytes),
      _checkpoints(_failure, configuration.maxCheckpoints),
      _items(_failure, itemCapacity(configuration)),
      _relations(_failure, _items, configuration.maxRelations, configuration.maxRelations * VERSION_FACTOR),
      _decisions(_failure, configuration.maxDecisionVersions),
      _file(SpecTables{_failure, _catalog, _meta, _checkpoints, _items, _relations, _decisions}, _work),
      _workspace(Workspace{_file, _failure, _catalog, _meta, _checkpoints, _items, _relations, _decisions}),
      _checker(_workspace),
      _lifecycle(_workspace, _checker),
      _editor(_workspace, _lifecycle),
      _relation_editor(_workspace, _lifecycle),
      _cursors(configuration.maxCursors, configuration.maxCursorRows),
      _finder(_workspace, _cursors, _checker, configuration.maxItems),
      _differ(_workspace, _cursors, _work)
{
}

// .kiotty/spec.kisp → .kiotty/spec. 확장자가 다르면 <파일>.body
SystemPathResult Context::bodyDirectory(Span<const char> database)
{
    const Span<const char> name = fileNameOf(database);
    const bool has_extension = name.length() > DATABASE_EXTENSION.size() && viewOf(name).substr(name.length() - DATABASE_EXTENSION.size()) == DATABASE_EXTENSION;
    if (has_extension) { return joinPath(parentOf(database), Span<const char>(name.data(), name.length() - DATABASE_EXTENSION.size())); }
    SystemPath directory;
    const KiottySpecStatus assigned = directory.assign(database);
    const KiottySpecStatus appended = directory.append(literalText(BODY_SUFFIX));
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS || appended != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    return SystemPathResult::success(directory);
}

KiottySpecStatus Context::open(Span<const char> database)
{
    if (_opened) { return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText(".kisp 가 이미 열려 있음")}); }
    if (database.length() == 0) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText(".kisp 경로가 비어 있음")}); }
    if (isDirectory(database)) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText(".kisp 경로가 디렉토리임: "), database}); }
    const auto absolute = absolutePath(database);
    if (!absolute) { return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 경로를 절대 경로로 바꿀 수 없음: "), database}); }
    const auto bodies = bodyDirectory(absolute.value.view());
    if (!bodies) { return _failure.raise(bodies.code, {literalText("경로가 너무 김: "), database}); }
    _root.assign(parentOf(parentOf(absolute.value.view())));
    const KiottySpecStatus catalog = _catalog.load();
    if (catalog != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(catalog, {literalText("모델 표를 적재할 수 없음")}); }
    const bool exists = isFile(absolute.value.view());
    const KiottySpecStatus loaded = _file.open(absolute.value.view(), bodies.value.view());
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    if (!exists && createDirectories(parentOf(absolute.value.view())) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 디렉토리를 만들 수 없음: "), absolute.value.view()});
    }
    const KiottySpecStatus created = exists ? KIOTTY_SPEC_STATUS_SUCCESS : _file.save();
    _opened = created == KIOTTY_SPEC_STATUS_SUCCESS;
    return created;
}

KiottySpecStatus Context::init(Span<const char> name)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_root.view());
    KiottySpecStatus status = _meta.set(literalText("project.name"), project);
    const int64_t seconds = nowEpochSeconds();
    FixedString<MAX_EPOCH_DIGITS> now;
    appendDecimal(now, seconds > 0 ? static_cast<uint64_t>(seconds) : 0);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _meta.set(literalText("project.created_at"), now.view()); }
    return _workspace.finish(status);
}

}
