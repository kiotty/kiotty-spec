#include "implementation/context/kiotty_context.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_writer.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_EPOCH_DIGITS = 20;

ItemCapacity itemCapacity(const Configuration& configuration)
{
    return ItemCapacity{configuration.maxItems, configuration.maxTags, configuration.maxItems * VERSION_FACTOR, configuration.maxTags * VERSION_FACTOR,
                        configuration.maxBodyBytes};
}

KiottySpecStatus writeMergeAttributes(Span<const char> directory)
{
    const SystemPathResult path = joinPath(directory, literalText(".gitattributes"));
    if (!path) { return path.code; }
    if (isFile(path.value.view())) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    FileWriter writer;
    const KiottySpecStatus opened = writer.open(path.value.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    return writer.write(literalText("*.kisp text eol=lf merge=union\n"));
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

KiottySpecStatus Context::open(Span<const char> database)
{
    if (_opened) { return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText(".kisp 가 이미 열려 있음")}); }
    if (database.length() == 0) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText(".kisp 경로가 비어 있음")}); }
    if (isDirectory(database)) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText(".kisp 경로가 디렉토리임: "), database}); }
    const auto absolute = absolutePath(database);
    if (!absolute) { return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 경로를 절대 경로로 바꿀 수 없음: "), database}); }
    _root.assign(parentOf(parentOf(absolute.value.view())));
    const KiottySpecStatus catalog = _catalog.load();
    if (catalog != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(catalog, {literalText("모델 표를 적재할 수 없음")}); }
    const bool exists = isFile(absolute.value.view());
    const KiottySpecStatus loaded = _file.open(absolute.value.view());
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    if (!exists && createDirectories(parentOf(absolute.value.view())) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText(".kisp 디렉토리를 만들 수 없음: "), absolute.value.view()});
    }
    const KiottySpecStatus created = exists ? KIOTTY_SPEC_STATUS_SUCCESS : _file.save();
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return created; }
    const KiottySpecStatus attributed = exists ? KIOTTY_SPEC_STATUS_SUCCESS : writeMergeAttributes(parentOf(absolute.value.view()));
    if (attributed != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(attributed, {literalText(".gitattributes 를 만들 수 없음")}); }
    _opened = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
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
