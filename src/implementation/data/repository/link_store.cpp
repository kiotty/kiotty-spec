#include "implementation/data/repository/link_store.h"

#include "implementation/core/text_view.h"
#include "implementation/data/repository/record_rows.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char EXISTS_SQL[] =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0"
    " AND v.from_id = ?2 AND v.to_id = ?3 AND v.type = ?4";

constexpr char LIST_SQL[] =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0"
    " AND ((?2 = 1 AND v.from_id = ?4) OR (?3 = 1 AND v.to_id = ?4)) AND (?5 = '' OR v.type = ?5)"
    " ORDER BY v.type, v.from_id, v.to_id";

constexpr KiottySpecLinkType ANY_TYPE = static_cast<KiottySpecLinkType>(0);

constexpr char WRITE_SQL[] =
    "INSERT INTO link_version (from_id, to_id, type, cp_seq, removed) VALUES (?1, ?2, ?3, ?4, ?5)"
    " ON CONFLICT(from_id, to_id, type, cp_seq) DO UPDATE SET removed = excluded.removed";

constexpr char ALLOWED_HELP[] =
    "허용되는 링크: interface satisfies requirement(functional) | design satisfies requirement/interface | "
    "unit_test verifies design | system_test verifies requirement(functional) | e2e_test verifies interface | "
    "file(gui_draft/c_header) implements interface | file(source) implements design | "
    "milestone covers design/unit_test | design(module) depends_on design(module)";



bool isItem(const LinkEnd &end, KiottySpecItemKind kind) { return !end.isFile && end.kind == kind; }

bool isFunctional(const LinkEnd &end)
{
    return isItem(end, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT) && end.category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
}

bool isModule(const LinkEnd &end)
{
    return isItem(end, KIOTTY_SPEC_ITEM_KIND_DESIGN) && end.category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE;
}

bool isFile(const LinkEnd &end, KiottySpecFileRole role) { return end.isFile && end.role == role; }

bool allowsSatisfies(const LinkEnd &from, const LinkEnd &to)
{
    const bool interfaceToFunctional = isItem(from, KIOTTY_SPEC_ITEM_KIND_INTERFACE) && isFunctional(to);
    const bool designTarget = isItem(to, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT) || isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE);
    const bool designToTarget = isItem(from, KIOTTY_SPEC_ITEM_KIND_DESIGN) && designTarget;
    return interfaceToFunctional || designToTarget;
}

bool allowsVerifies(const LinkEnd &from, const LinkEnd &to)
{
    const bool unit = isItem(from, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST) && isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    const bool system = isItem(from, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST) && isFunctional(to);
    const bool endToEnd = isItem(from, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST) && isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE);
    return unit || system || endToEnd;
}

bool allowsImplements(const LinkEnd &from, const LinkEnd &to)
{
    const bool isInterfaceFile = isFile(from, KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT) || isFile(from, KIOTTY_SPEC_FILE_ROLE_C_HEADER);
    const bool interface = isInterfaceFile && isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE);
    const bool source = isFile(from, KIOTTY_SPEC_FILE_ROLE_SOURCE) && isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    return interface || source;
}

bool allowsCovers(const LinkEnd &from, const LinkEnd &to)
{
    const bool coverable = isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN) || isItem(to, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST);
    return isItem(from, KIOTTY_SPEC_ITEM_KIND_MILESTONE) && coverable;
}

bool allowed(KiottySpecLinkType type, const LinkEnd &from, const LinkEnd &to)
{
    switch (type) {
    case KIOTTY_SPEC_LINK_TYPE_SATISFIES:
        return allowsSatisfies(from, to);
    case KIOTTY_SPEC_LINK_TYPE_VERIFIES:
        return allowsVerifies(from, to);
    case KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS:
        return allowsImplements(from, to);
    case KIOTTY_SPEC_LINK_TYPE_COVERS:
        return allowsCovers(from, to);
    case KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON:
        return isModule(from) && isModule(to);
    }
    return false;
}


}

KiottySpecStatus LinkStore::add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type)
{
    const KiottySpecStatus validated = validate(fromId, toId, type);
    if (validated != KIOTTY_SPEC_STATUS_SUCCESS) { return validated; }
    const auto present = exists(fromId, toId, type);
    if (!present) { return present.code; }
    if (present.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return writeVersion(fromId, toId, type, false);
}

KiottySpecStatus LinkStore::remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type)
{
    const auto present = exists(fromId, toId, type);
    if (!present) { return present.code; }
    if (!present.value) { return _storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("링크 없음: "), spanOf(fromId), literalText(" "), spanOf(name(type)), literalText(" "), spanOf(toId)}); }
    return writeVersion(fromId, toId, type, true);
}

KiottySpecStatus LinkStore::removeAllFor(std::string_view id)
{
    const auto links = list(id, KIOTTY_SPEC_DIRECTION_BOTH, ANY_TYPE);
    if (!links) { return links.code; }
    for (const LinkRecord &link : links.value) {
        const KiottySpecStatus written = writeVersion(link.fromId, link.toId, link.type, true);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

FlagResult LinkStore::exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut)
{
    auto query = _storage.prepare(literalText(EXISTS_SQL));
    if (!query) { return FlagResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(fromId));
    query.value.bind(3, spanOf(toId));
    query.value.bind(4, spanOf(name(type)));
    const auto stepped = query.value.step();
    if (!stepped) { return FlagResult::failure(_storage.check(stepped.code)); }
    return FlagResult::success(stepped.value);
}

LinkListResult LinkStore::list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, int64_t cut)
{
    auto query = _storage.prepare(literalText(LIST_SQL));
    if (!query) { return LinkListResult::failure(query.code); }
    const bool up = dir == KIOTTY_SPEC_DIRECTION_UPSTREAM || dir == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool down = dir == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || dir == KIOTTY_SPEC_DIRECTION_BOTH;
    const std::string_view type = typeOr0 != 0 ? name(typeOr0) : std::string_view();
    query.value.bind(1, cut);
    query.value.bind(2, static_cast<int64_t>(up ? 1 : 0));
    query.value.bind(3, static_cast<int64_t>(down ? 1 : 0));
    query.value.bind(4, spanOf(id));
    query.value.bind(5, spanOf(type));
    return collectLinks(_storage, query.value);
}

TextListResult LinkStore::targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut)
{
    auto links = list(fromId, KIOTTY_SPEC_DIRECTION_UPSTREAM, type, cut);
    if (!links) { return TextListResult::failure(links.code); }
    std::vector<std::string> ids;
    for (LinkRecord &link : links.value) { ids.push_back(std::string(link.toId)); }
    return TextListResult::success(std::move(ids));
}

TextListResult LinkStore::sources(std::string_view toId, KiottySpecLinkType type, int64_t cut)
{
    auto links = list(toId, KIOTTY_SPEC_DIRECTION_DOWNSTREAM, type, cut);
    if (!links) { return TextListResult::failure(links.code); }
    std::vector<std::string> ids;
    for (LinkRecord &link : links.value) { ids.push_back(std::string(link.fromId)); }
    return TextListResult::success(std::move(ids));
}

KiottySpecStatus LinkStore::validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type)
{
    if (fromId == toId) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("자기 자신으로의 링크는 만들 수 없음")}); }
    const auto from = endOf(fromId);
    if (!from) { return from.code; }
    const auto to = endOf(toId);
    if (!to) { return to.code; }
    if (allowed(type, from.value, to.value)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {spanOf(fromId), literalText(" "), spanOf(name(type)), literalText(" "), spanOf(toId), literalText(" 는 허용되지 않음. "), literalText(ALLOWED_HELP)});
}

Result<LinkEnd, KiottySpecStatus> LinkStore::endOf(std::string_view id)
{
    using EndResult = Result<LinkEnd, KiottySpecStatus>;
    LinkEnd end;
    const auto item = _items.find(id);
    if (!item) { return EndResult::failure(item.code); }
    if (item.value) {
        end.kind = item.value->kind;
        end.category = item.value->category;
        return EndResult::success(end);
    }
    const auto file = _files.find(id);
    if (!file) { return EndResult::failure(file.code); }
    if (!file.value) { return EndResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("항목·파일 없음: "), spanOf(id)})); }
    end.isFile = true;
    end.role = file.value->role;
    return EndResult::success(end);
}

KiottySpecStatus LinkStore::writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed)
{
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(fromId));
    query.value.bind(2, spanOf(toId));
    query.value.bind(3, spanOf(name(type)));
    query.value.bind(4, kPending);
    query.value.bind(5, static_cast<int64_t>(removed ? 1 : 0));
    return _storage.check(query.value.run());
}

}
