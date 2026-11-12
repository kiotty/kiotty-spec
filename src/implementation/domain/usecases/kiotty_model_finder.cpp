#include "implementation/domain/usecases/kiotty_model_finder.h"

#include "implementation/core/kiotty_checked.h"
#include "implementation/core/kiotty_search.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

const entity::Model EMPTY_MODEL {};

}

const entity::Model& pickModel(Span<const entity::Model> models, ItemKind kind)
{
    const size_t index = entity::kindIndex(kind);
    const auto found = models.at(index);
    if (!found) { return EMPTY_MODEL; }
    return *found.value;
}

Result<uint32_t, KiottySpecStatus> ModelFinder::findTag(ItemKind kind, Span<const char> name) const
{
    return findIndex(tagsOf(kind), [&](const entity::TagDefinition& definition) {
        return std::string_view(definition.name) == viewOf(name);
    });
}

Result<uint32_t, KiottySpecStatus> ModelFinder::findTagBit(ItemKind kind, uint32_t tag,
                                                           Span<const char> name) const
{
    const Span<const entity::TagName> names = bitNamesOf(kind, tag);
    const bool is_unknown_tag = names.length() == 0;
    if (is_unknown_tag) {
        return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const std::string_view wanted = viewOf(name);
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        const entity::TagName& bit_name = names[bit];
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
    return findIndex(relationsOf(kind), [&](const entity::RelationDefinition& definition) {
        return std::string_view(definition.name) == viewOf(name);
    });
}

Span<const entity::TagDefinition> ModelFinder::tagsOf(ItemKind kind) const
{
    const IndexRange range = model(kind).tags;
    const auto window = _tables.tags.subspan(range.first, range.length);
    return window.value;
}

Span<const entity::RelationDefinition> ModelFinder::relationsOf(ItemKind kind) const
{
    const IndexRange range = model(kind).relations;
    const auto window = _tables.relations.subspan(range.first, range.length);
    return window.value;
}

Span<const entity::TagName> ModelFinder::bitNamesOf(ItemKind kind, uint32_t tag) const
{
    const IndexRange range = model(kind).tags;
    const bool is_known_tag = tag < range.length;
    if (!is_known_tag) { return Span<const entity::TagName>(); }
    const Result<size_t, KiottySpecStatus> first_bit =
        checkedMultiply(range.first + tag, entity::TAG_BIT_COUNT);
    if (!first_bit) { return Span<const entity::TagName>(); }
    const auto window = _tables.bit_names.subspan(first_bit.value, entity::TAG_BIT_COUNT);
    return window.value;
}

}
