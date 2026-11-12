#include "implementation/context/kiotty_context.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_writer.h"

namespace kiotty::spec::implementation {

namespace {

ItemCapacity buildItemCapacity(const Configuration& configuration);
KiottySpecStatus writeMergeAttributes(const PathResolver& path_resolver,
                                      const FileSystem& file_system, Span<const char> directory);

}

Context::Context(const Configuration& configuration)
    : _path_resolver(_paths), _file_system(_paths), _work(configuration.work_buffer_bytes),
      _checkpoints(_failure, configuration.max_checkpoints),
      _items(_failure, buildItemCapacity(configuration)),
      _relations(_failure, _items, configuration.max_relations,
                 configuration.max_relation_versions),
      _decisions(_failure, configuration.max_decision_versions),
      _file(SpecTables { _failure, _catalog, _meta, _checkpoints, _items, _relations, _decisions },
            _work, _file_system),
      _workspace(Workspace { _file, _failure, _catalog, _meta, _checkpoints, _items, _relations,
                             _decisions }),
      _checker(_workspace, configuration.max_items), _lifecycle(_workspace, _checker),
      _editor(_workspace, _lifecycle), _relation_editor(_workspace, _lifecycle),
      _cursors(configuration.max_cursors, configuration.max_cursor_rows,
               configuration.cursor_pool_rows),
      _finder(_workspace, _cursors, _checker, configuration.max_items),
      _text_diffs(configuration.max_cursors), _differ(_workspace, _cursors, _text_diffs)
{
}

KiottySpecStatus Context::open(Span<const char> database)
{
    const KiottySpecStatus checked = checkDatabase(database);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    const SystemPathResult absolute = _path_resolver.resolveAbsolutePath(database);
    if (!absolute) { return _failure.raise(absolute.code, KIOTTY_SPEC_SUBJECT_FILE, database); }
    const SystemPath& absolute_path = *absolute.value;
    const Span<const char> directory = parentOf(absolute_path.view());
    _root.assign(parentOf(directory));
    const KiottySpecStatus catalog = _catalog.load();
    if (catalog != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(catalog, KIOTTY_SPEC_SUBJECT_MODEL);
    }
    const bool is_existing = _file_system.isFile(absolute_path.view());
    const KiottySpecStatus loaded = _file.open(absolute_path.view());
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    const KiottySpecStatus created =
        is_existing ? KIOTTY_SPEC_STATUS_SUCCESS : createSpecFile(directory);
    if (created != KIOTTY_SPEC_STATUS_SUCCESS) { return created; }
    _is_opened = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::checkDatabase(Span<const char> database)
{
    const bool is_opened = _is_opened;
    if (is_opened) {
        return _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_FILE, database);
    }
    const size_t database_length = database.length();
    if (database_length == 0) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE);
    }
    const bool is_directory = _file_system.isDirectory(database);
    if (is_directory) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE,
                              database);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::createSpecFile(Span<const char> directory)
{
    const KiottySpecStatus prepared = _file_system.createDirectories(directory);
    if (prepared != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, KIOTTY_SPEC_SUBJECT_FILE, directory);
    }
    const KiottySpecStatus saved = _file.save();
    if (saved != KIOTTY_SPEC_STATUS_SUCCESS) { return saved; }
    const KiottySpecStatus attributed =
        writeMergeAttributes(_path_resolver, _file_system, directory);
    if (attributed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(attributed, KIOTTY_SPEC_SUBJECT_FILE, directory);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Context::initialize(Span<const char> name)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_root.view());
    KiottySpecStatus status = _meta.set(literalText("project.name"), project);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _failure.raise(status, KIOTTY_SPEC_SUBJECT_PROJECT_NAME, project);
    }
    const int64_t seconds = nowEpochSeconds();
    const DecimalText now = formatDecimal(seconds > 0 ? static_cast<uint64_t>(seconds) : 0);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _meta.set(literalText("project.created_at"), now.view());
    }
    return _workspace.finish(status);
}

namespace {

ItemCapacity buildItemCapacity(const Configuration& configuration)
{
    return ItemCapacity { configuration.max_items, configuration.max_tags,
                          configuration.max_item_versions, configuration.max_tag_versions,
                          configuration.max_body_bytes };
}

KiottySpecStatus writeMergeAttributes(const PathResolver& path_resolver,
                                      const FileSystem& file_system, Span<const char> directory)
{
    const SystemPathResult path = path_resolver.joinPath(directory, literalText(".gitattributes"));
    if (!path) { return path.code; }
    const SystemPath& attributes_path = *path.value;
    const bool is_file = file_system.isFile(attributes_path.view());
    if (is_file) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    FileWriter writer(file_system);
    const KiottySpecStatus opened = writer.open(attributes_path.view());
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    return writer.write(literalText("*.kisp text eol=lf merge=union\n"));
}

}

}
