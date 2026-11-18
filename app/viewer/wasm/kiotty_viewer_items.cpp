#include "kiotty_viewer_query.h"

#include "kiotty_viewer_names.h"

#include <memory>

namespace kiotty::viewer {

void QueryWriter::writeItems(KiottySpecItemKind kind, ItemListStyle style)
{
    _json.open(JsonScope::OBJECT);
    writeSuccess();
    _json.writeKey("kind");
    _json.writeString(formatKind(kind));
    writeModel(kind);
    writeItemList(kind, style);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeModel(KiottySpecItemKind kind)
{
    const KiottySpecModelResult model = KiottySpec_readModel(&_context, kind);
    const KindDefinitions& definitions = _definitions.findKind(kind);
    _json.writeKey("model");
    _json.open(JsonScope::OBJECT);
    _json.writeKey("name");
    _json.writeString(model.value.name);
    _json.writeKey("prefix");
    _json.writeString(model.value.id_prefix);
    _json.writeKey("created_in");
    _json.writeNumber(static_cast<uint64_t>(model.value.created_in));
    _json.writeKey("tags");
    _json.open(JsonScope::ARRAY);
    for (const KiottySpecTagDefinition& definition : definitions.tags) {
        writeTagDefinition(definition);
    }
    _json.close(JsonScope::ARRAY);
    _json.writeKey("relations");
    _json.open(JsonScope::ARRAY);
    for (const KiottySpecRelationDefinition& definition : definitions.relations) {
        writeRelationDefinition(definition);
    }
    _json.close(JsonScope::ARRAY);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeTagDefinition(const KiottySpecTagDefinition& definition)
{
    const bool is_single = definition.cardinality == KIOTTY_SPEC_CARDINALITY_SINGLE;
    _json.open(JsonScope::OBJECT);
    _json.writeKey("index");
    _json.writeNumber(definition.index);
    _json.writeKey("name");
    _json.writeString(definition.name);
    _json.writeKey("cardinality");
    _json.writeString(is_single ? "single" : "many");
    _json.writeKey("values");
    _json.open(JsonScope::ARRAY);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const std::string_view name = definition.bit_names[bit];
        const bool is_named = !name.empty();
        if (is_named) { _json.writeString(name); }
    }
    _json.close(JsonScope::ARRAY);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeRelationDefinition(const KiottySpecRelationDefinition& definition)
{
    _json.open(JsonScope::OBJECT);
    _json.writeKey("index");
    _json.writeNumber(definition.index);
    _json.writeKey("name");
    _json.writeString(definition.name);
    _json.writeKey("target");
    _json.writeString(formatKind(definition.target_kind));
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeItemList(KiottySpecItemKind kind, ItemListStyle style)
{
    _json.writeKey("items");
    _json.open(JsonScope::ARRAY);
    const KiottySpecCursorResult cursor = KiottySpec_openItemCursor(&_context, kind, nullptr, 0);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) {
        _json.close(JsonScope::ARRAY);
        return;
    }
    const std::unique_ptr<KiottySpecItemPage> page_storage = std::make_unique<KiottySpecItemPage>();
    KiottySpecItemPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchItemPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            writeItemRow(page.elements[index], style);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeItemRow(const KiottySpecItem& item, ItemListStyle style)
{
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(&_context, makeText(item.id));
    const bool is_with_out_count = style == ItemListStyle::WITH_OUT_COUNT;
    _json.open(JsonScope::OBJECT);
    writeItemHead(item);
    _json.writeKey("tags");
    writeItemTags(handle.value, item.kind);
    if (is_with_out_count) {
        _json.writeKey("out_count");
        _json.writeNumber(countOutRelations(item, handle.value));
    }
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeItemHead(const KiottySpecItem& item)
{
    const KiottySpecItemResult parent = KiottySpec_readItemByHandle(&_context, item.parent);
    const bool has_parent = isSuccess(parent.code);
    _json.writeKey("id");
    _json.writeString(item.id);
    _json.writeKey("kind");
    _json.writeString(formatKind(item.kind));
    _json.writeKey("title");
    _json.writeString(item.title);
    _json.writeKey("summary");
    _json.writeString(item.summary);
    _json.writeKey("checkpoint");
    writeCheckpointOrNull(item.checkpoint);
    _json.writeKey("parent");
    if (has_parent) { _json.writeString(parent.value.id); }
    else { _json.writeLiteral("null"); }
}

void QueryWriter::writeItemTags(KiottySpecItemHandle item, KiottySpecItemKind kind)
{
    _json.open(JsonScope::OBJECT);
    const KiottySpecCursorResult cursor = KiottySpec_openTagCursor(&_context, item);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) {
        _json.close(JsonScope::OBJECT);
        return;
    }
    const KindDefinitions& model = _definitions.findKind(kind);
    const std::unique_ptr<KiottySpecTagPage> page_storage = std::make_unique<KiottySpecTagPage>();
    KiottySpecTagPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchTagPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            writeTag(page.elements[index], model);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeTag(const KiottySpecTag& tag, const KindDefinitions& model)
{
    for (const KiottySpecTagDefinition& definition : model.tags) {
        const bool is_match = definition.index == tag.tag;
        if (!is_match) { continue; }
        _json.writeKey(definition.name);
        writeTagValues(definition, tag.value);
    }
}

void QueryWriter::writeTagValues(const KiottySpecTagDefinition& definition, uint64_t value)
{
    _json.open(JsonScope::ARRAY);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const bool is_set = (value & (uint64_t { 1 } << bit)) != 0;
        if (!is_set) { continue; }
        const std::string_view name = definition.bit_names[bit];
        const bool is_named = !name.empty();
        _json.writeString(is_named ? std::string(name) : std::to_string(bit));
    }
    _json.close(JsonScope::ARRAY);
}

uint64_t QueryWriter::countOutRelations(const KiottySpecItem& item, KiottySpecItemHandle self)
{
    uint64_t count = 0;
    for (const KiottySpecRelation& relation : readRelations(item)) {
        const bool is_out = relation.from == self;
        if (is_out) { ++count; }
    }
    return count;
}

std::vector<KiottySpecRelation> QueryWriter::readRelations(const KiottySpecItem& item)
{
    std::vector<KiottySpecRelation> rows {};
    const KiottySpecCursorResult cursor =
        KiottySpec_openRelationCursor(&_context, makeText(item.id), KIOTTY_SPEC_DIRECTION_BOTH);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) { return rows; }
    const std::unique_ptr<KiottySpecRelationPage> page_storage =
        std::make_unique<KiottySpecRelationPage>();
    KiottySpecRelationPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            rows.push_back(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    return rows;
}

}
