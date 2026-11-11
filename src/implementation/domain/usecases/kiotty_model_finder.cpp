#include "implementation/domain/usecases/kiotty_model_finder.h"

#include "implementation/core/kiotty_search.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

}

Result<uint32_t, KiottySpecStatus> ModelFinder::findTag(ItemKind kind, Span<const char> name) const
{
    return findIndex(model(kind).tags, [&](const entity::TagDefinition& definition) {
        return std::string_view(definition.name) == viewOf(name);
    });
}

Result<uint32_t, KiottySpecStatus> ModelFinder::findTagBit(ItemKind kind, uint32_t tag,
                                                           Span<const char> name) const
{
    const Span<const entity::TagDefinition> tags = model(kind).tags;
    const size_t tag_count = tags.length();
    if (tag >= tag_count) {
        return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const std::string_view wanted = viewOf(name);
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        const entity::TagName& bit_name = tags[tag].bit_names[bit];
        const bool is_empty = bit_name.empty();
        if (is_empty) { continue; }
        const std::string_view bit_text = bit_name;
        if (bit_text == wanted) {
            return Result<uint32_t, KiottySpecStatus>::success(static_cast<uint32_t>(bit));
        }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
}

Result<uint32_t, KiottySpecStatus> ModelFinder::findRelation(ItemKind kind,
                                                             Span<const char> name) const
{
    return findIndex(model(kind).relations, [&](const entity::RelationDefinition& definition) {
        return std::string_view(definition.name) == viewOf(name);
    });
}

}
