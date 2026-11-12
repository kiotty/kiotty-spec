#include "implementation/context/kiotty_context.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_writer.h"

#include <utility>

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
            _work, _file_system, _database),
      _workspace(Workspace { _file, _failure, _catalog, _meta, _checkpoints, _items, _relations,
                             _decisions }),
      _checker(_workspace, configuration.max_items), _lifecycle(_workspace, _checker),
      _editor(_workspace), _relation_editor(_workspace),
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
    const KiottySpecStatus loaded = loadSpecFile(absolute_path.view());
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    _opened.emplace(_workspace, ContextWriteSide(_editor, _relation_editor, _lifecycle),
                    ContextReadSide(_catalog, _finder, _differ, _cursors), _root);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

OpenedContextResult Context::opened()
{
    const bool is_opened = _opened.has_value();
    if (!is_opened) {
        return OpenedContextResult::failure(
            _failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_FILE));
    }
    return OpenedContextResult::success(*_opened);
}

KiottySpecStatus Context::loadSpecFile(Span<const char> absolute_path)
{
    const KiottySpecStatus assigned = _database.assign(absolute_path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(assigned, KIOTTY_SPEC_SUBJECT_FILE, absolute_path);
    }
    const bool is_existing = _file_system.isFile(absolute_path);
    const KiottySpecStatus loaded = _file.load();
    if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
    if (is_existing) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return createSpecFile(parentOf(absolute_path));
}

KiottySpecStatus Context::checkDatabase(Span<const char> database)
{
    const bool is_opened = _opened.has_value();
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
    OpenedFileResult opened = OpenedFile::openForWriting(file_system, attributes_path.view());
    if (!opened) { return opened.code; }
    FileWriter writer(std::move(opened.value));
    return writer.write(literalText("*.kisp text eol=lf merge=union\n"));
}

}

}
