#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>
#include <optional>

namespace kiotty::spec::implementation {

using OptionalText = std::optional<Span<const char>>;

class StructureEditor
{
public:
    explicit StructureEditor(Workspace& workspace) : _workspace(workspace) {}

    Result<entity::IdText, KiottySpecStatus> createItem(entity::ItemKind kind, Span<const char> id,
                                                        Span<const char> title,
                                                        Span<const char> summary);
    KiottySpecStatus renameItem(Span<const char> id, Span<const char> new_id);
    KiottySpecStatus setParent(Span<const char> id, Span<const char> parent_id);
    KiottySpecStatus removeItem(Span<const char> id);

private:
    Result<entity::IdText, KiottySpecStatus> insertItem(const entity::ItemRecord& draft,
                                                        Span<const char> id, uint32_t sequence);
    Result<entity::ItemHandle, KiottySpecStatus> resolveParent(entity::ItemHandle item,
                                                               Span<const char> id,
                                                               Span<const char> parent_id);
    KiottySpecStatus checkId(Span<const char> id);
    KiottySpecStatus checkNoChildren(entity::ItemHandle item);

    Workspace& _workspace;
};

class ContentEditor
{
public:
    explicit ContentEditor(Workspace& workspace) : _workspace(workspace) {}

    KiottySpecStatus updateItem(Span<const char> id, OptionalText title, OptionalText summary);
    KiottySpecStatus writeBody(Span<const char> id, Span<const char> body);
    KiottySpecStatus setTag(entity::ItemHandle item, uint32_t tag, uint64_t value);
    KiottySpecStatus writeDecision(entity::ItemKind kind, Span<const char> text);

private:
    KiottySpecStatus markNeedsChange(entity::ItemHandle item, uint32_t sequence);

    Workspace& _workspace;
};

class Editor
{
public:
    explicit Editor(Workspace& workspace) : _workspace(workspace) {}

    StructureEditor structure() { return StructureEditor(_workspace); }
    ContentEditor content() { return ContentEditor(_workspace); }

private:
    Workspace& _workspace;
};

}
