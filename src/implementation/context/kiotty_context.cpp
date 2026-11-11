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
    return ItemCapacity { configuration.max_items, configuration.max_tags,
                          configuration.max_item_versions, configuration.max_tag_versions,
                          configuration.max_body_bytes };
}

KiottySpecStatus writeMergeAttributes(Span<const char> directory)
{
    const SystemPathResult path = joinPath(directory, literalText(".gitattributes"));
    if (!path) { return path.code; }
    const bool is_file = isFile(path.value.view());
    if (is_file) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    FileWriter writer {};
    const KiottySpecStatus opened = writer.open(path.value.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    return writer.write(literalText("*.kisp text eol=lf merge=union\n"));
}

}

Context::Context(const Configuration& configuration)
    : _work(configuration.work_buffer_bytes), _checkpoints(_failure, configuration.max_checkpoints),
      _items(_failure, itemCapacity(configuration)),
      _relations(_failure, _items, configuration.max_relations,
                 configuration.max_relation_versions),
      _decisions(_failure, configuration.max_decision_versions),
      _file(SpecTables { _failure, _catalog, _meta, _checkpoints, _items, _relations, _decisions },
            _work),
      _workspace(Workspace { _file, _failure, _catalog, _meta, _checkpoints, _items, _relations,
                             _decisions }),
      _checker(_workspace, configuration.max_items), _lifecycle(_workspace, _checker),
      _editor(_workspace, _lifecycle), _relation_editor(_workspace, _lifecycle),
      _cursors(configuration.max_cursors, configuration.max_cursor_rows,
               configuration.cursor_pool_rows),
      _finder(_workspace, _cursors, _checker, configuration.max_items),
      _differ(_workspace, _cursors, _work)
{
}

KiottySpecStatus Context::open(Span<const char> database)
{
    if (_opened) {
        return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_FILE, database);
    }
    const size_t database_length = database.length();
    if (database_length == 0) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE);
    }
    const bool is_directory = isDirectory(database);
    if (is_directory) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE,
                              database);
    }
    const auto absolute = absolutePath(database);
    if (!absolute) { return _failure.raise(absolute.code, KIOTTY_SPEC_SUBJECT_FILE, database); }
    _root.assign(parentOf(parentOf(absolute.value.view())));
    const KiottySpecStatus catalog = _catalog.load();
    if (catalog != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(catalog, KIOTTY_SPEC_SUBJECT_MODEL);
    }
    const bool exists = isFile(absolute.value.view());
    const KiottySpecStatus loaded = _file.open(absolute.value.view());
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    const KiottySpecStatus prepared =
        exists ? KIOTTY_SPEC_STATUS_SUCCESS : createDirectories(parentOf(absolute.value.view()));
    if (prepared != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE,
                              parentOf(absolute.value.view()));
    }
    const KiottySpecStatus created = exists ? KIOTTY_SPEC_STATUS_SUCCESS : _file.save();
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return created; }
    const KiottySpecStatus attributed =
        exists ? KIOTTY_SPEC_STATUS_SUCCESS : writeMergeAttributes(parentOf(absolute.value.view()));
    if (attributed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(attributed, KIOTTY_SPEC_SUBJECT_FILE,
                              parentOf(absolute.value.view()));
    }
    _opened = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::init(Span<const char> name)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_root.view());
    KiottySpecStatus status = _meta.set(literalText("project.name"), project);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _failure.raise(status, KIOTTY_SPEC_SUBJECT_PROJECT_NAME, project);
    }
    const int64_t seconds = nowEpochSeconds();
    FixedString<MAX_EPOCH_DIGITS> now {};
    appendDecimal(now, seconds > 0 ? static_cast<uint64_t>(seconds) : 0);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _meta.set(literalText("project.created_at"), now.view());
    }
    return _workspace.finish(status);
}

}
