#include "implementation/data/repository/item_finder.h"

#include "implementation/core/text_view.h"
#include "implementation/data/repository/record_rows.h"

#include <set>
#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr char CHILDREN_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0 AND v.parent_id = ?2 ORDER BY v.seq, v.item_id";
constexpr char CHANGED_IDS_SQL[] =
    "SELECT DISTINCT item_id FROM item_version WHERE cp_seq > ?1 AND cp_seq <= ?2 ORDER BY item_id";
constexpr char VERSION_AT_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v WHERE v.item_id = ?1 AND v.cp_seq <= ?2 ORDER BY v.cp_seq DESC LIMIT 1";
constexpr char REMOVED_AT_SQL[] =
    "SELECT removed FROM item_version WHERE item_id = ?1 AND cp_seq <= ?2 ORDER BY cp_seq DESC LIMIT 1";

}

ItemListResult ItemFinder::ofKind(KiottySpecItemKind kind, int64_t cut)
{
    ItemQuery query;
    query.kind = kind;
    query.cut = cut;
    return _items.list(query);
}

ItemListResult ItemFinder::children(std::string_view id, bool recursive, int64_t cut)
{
    const auto root = _items.get(id, cut);
    if (!root) { return ItemListResult::failure(root.code); }
    std::vector<ItemRecord> result;
    std::vector<std::string> frontier{std::string(id)};
    std::set<std::string> seen{std::string(id)};
    while (!frontier.empty()) {
        const std::string parent = frontier.back();
        frontier.pop_back();
        auto found = childrenOf(parent, cut);
        if (!found) { return found; }
        for (ItemRecord &child : found.value) {
            if (!seen.insert(std::string(child.id)).second) { continue; }
            if (recursive) { frontier.push_back(std::string(child.id)); }
            result.push_back(std::move(child));
        }
    }
    return ItemListResult::success(std::move(result));
}

ItemListResult ItemFinder::search(std::string_view query, size_t limit)
{
    ItemQuery itemQuery;
    itemQuery.text = std::string(query);
    auto records = _items.list(itemQuery);
    if (!records) { return records; }
    const bool isOverLimit = limit > 0 && records.value.size() > limit;
    if (isOverLimit) { records.value.resize(limit); }
    return records;
}

ItemChangeListResult ItemFinder::changedBetween(int64_t fromSeq, int64_t toSeq)
{
    auto ids = _storage.prepare(literalText(CHANGED_IDS_SQL));
    if (!ids) { return ItemChangeListResult::failure(ids.code); }
    ids.value.bind(1, fromSeq);
    ids.value.bind(2, toSeq);
    std::vector<ItemChange> changes;
    auto stepped = ids.value.step();
    while (stepped.value) {
        const std::string id(viewOf(ids.value.row().text(0)));
        auto change = changeOf(id, fromSeq, toSeq);
        if (!change) { return ItemChangeListResult::failure(change.code); }
        if (change.value) { changes.push_back(std::move(*change.value)); }
        stepped = ids.value.step();
    }
    if (!stepped) { return ItemChangeListResult::failure(_storage.check(stepped.code)); }
    return ItemChangeListResult::success(std::move(changes));
}

ItemListResult ItemFinder::childrenOf(std::string_view parentId, int64_t cut)
{
    auto query = _storage.prepare(literalText(CHILDREN_SQL));
    if (!query) { return ItemListResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(parentId));
    return collectItems(_storage, query.value);
}

OptionalItemChangeResult ItemFinder::changeOf(std::string_view id, int64_t fromSeq, int64_t toSeq)
{
    auto latest = _storage.prepare(literalText(VERSION_AT_SQL));
    if (!latest) { return OptionalItemChangeResult::failure(latest.code); }
    latest.value.bind(1, spanOf(id));
    latest.value.bind(2, toSeq);
    const auto found = latest.value.step();
    if (!found) { return OptionalItemChangeResult::failure(_storage.check(found.code)); }
    if (!found.value) { return OptionalItemChangeResult::success(std::nullopt); }
    auto record = readItem(_storage, latest.value.row());
    if (!record) { return OptionalItemChangeResult::failure(record.code); }
    ItemChange change;
    change.record = std::move(record.value);
    auto before = _storage.prepare(literalText(REMOVED_AT_SQL));
    if (!before) { return OptionalItemChangeResult::failure(before.code); }
    before.value.bind(1, spanOf(id));
    before.value.bind(2, fromSeq);
    const auto existed = before.value.step();
    if (!existed) { return OptionalItemChangeResult::failure(_storage.check(existed.code)); }
    change.added = !existed.value || before.value.row().integer(0) != 0;
    return OptionalItemChangeResult::success(std::move(change));
}

}
