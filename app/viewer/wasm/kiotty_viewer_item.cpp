#include "kiotty_viewer_query.h"

#include "kiotty_viewer_names.h"

#include <memory>

namespace kiotty::viewer {

namespace {

constexpr uint32_t IMPACT_DEPTH = 1;

}

void QueryWriter::writeItem(const KiottySpecItemResult& item)
{
    const KiottySpecItemResult parent = KiottySpec_readItemByHandle(&_context, item.value.parent);
    const bool has_parent = isSuccess(parent.code);
    _json.open(JsonScope::OBJECT);
    writeSuccess();
    _json.writeKey("item");
    _json.open(JsonScope::OBJECT);
    writeItemHead(item.value);
    _json.writeKey("parent_title");
    _json.writeString(has_parent ? parent.value.title : "");
    _json.close(JsonScope::OBJECT);
    _json.writeKey("tags");
    writeItemTags(item.handle, item.value.kind);
    writeBody(item.value);
    writeRelations(item.value, item.handle);
    writeImpact(item.value);
    writeChildren(item.value);
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeBody(const KiottySpecItem& item)
{
    std::vector<char> buffer(KIOTTY_SPEC_MAX_BODY_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readItemBody(&_context, makeText(item.id),
        KiottySpecBuffer { buffer.data(), buffer.size() });
    const bool has_body = isSuccess(read.code);
    buffer.resize(has_body ? read.value : 0);
    _json.writeKey("body");
    _json.writeString(std::string(buffer.begin(), buffer.end()));
}

void QueryWriter::writeRelations(const KiottySpecItem& item, KiottySpecItemHandle self)
{
    const std::vector<KiottySpecRelation> rows = readRelations(item);
    _json.writeKey("out");
    _json.open(JsonScope::ARRAY);
    for (const KiottySpecRelation& relation : rows) {
        const bool is_out = relation.from == self;
        if (is_out) { writeRelationSide(relation, relation.to); }
    }
    _json.close(JsonScope::ARRAY);
    _json.writeKey("in");
    _json.open(JsonScope::ARRAY);
    for (const KiottySpecRelation& relation : rows) {
        const bool is_in = relation.from != self;
        if (is_in) { writeRelationSide(relation, relation.from); }
    }
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeRelationSide(const KiottySpecRelation& relation, KiottySpecItemHandle other)
{
    const KiottySpecItemResult item = KiottySpec_readItemByHandle(&_context, other);
    const bool has_item = isSuccess(item.code);
    _json.open(JsonScope::OBJECT);
    _json.writeKey("relation");
    _json.writeString(_definitions.formatRelation(relation.model, relation.relation));
    _json.writeKey("id");
    _json.writeString(has_item ? item.value.id : "");
    _json.writeKey("kind");
    _json.writeString(has_item ? formatKind(item.value.kind) : "");
    _json.writeKey("title");
    _json.writeString(has_item ? item.value.title : "");
    _json.close(JsonScope::OBJECT);
}

void QueryWriter::writeImpact(const KiottySpecItem& item)
{
    _json.writeKey("impact");
    _json.open(JsonScope::ARRAY);
    const KiottySpecCursorResult cursor =
        KiottySpec_openImpactCursor(&_context, makeText(item.id), IMPACT_DEPTH);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) {
        _json.close(JsonScope::ARRAY);
        return;
    }
    const std::unique_ptr<KiottySpecIdPage> page_storage = std::make_unique<KiottySpecIdPage>();
    KiottySpecIdPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchIdPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            writeItemReference(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeChildren(const KiottySpecItem& item)
{
    _json.writeKey("children");
    _json.open(JsonScope::ARRAY);
    const KiottySpecCursorResult cursor = KiottySpec_openChildCursor(&_context, makeText(item.id));
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
            writeItemReference(page.elements[index].id);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    _json.close(JsonScope::ARRAY);
}

void QueryWriter::writeItemReference(std::string_view id)
{
    const KiottySpecItemResult item = KiottySpec_readItem(&_context, makeText(id));
    const bool has_item = isSuccess(item.code);
    _json.open(JsonScope::OBJECT);
    _json.writeKey("id");
    _json.writeString(id);
    _json.writeKey("kind");
    _json.writeString(has_item ? formatKind(item.value.kind) : "");
    _json.writeKey("title");
    _json.writeString(has_item ? item.value.title : "");
    _json.close(JsonScope::OBJECT);
}

}
