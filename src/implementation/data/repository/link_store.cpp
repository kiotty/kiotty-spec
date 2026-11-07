#include "implementation/data/repository/link_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr int32_t DEFAULT_TRAVERSE_DEPTH = 64;
constexpr std::string_view GUI_ID_ATTRIBUTE = "data-ks-id";

constexpr char EXISTS_SQL[] =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0"
    " AND v.from_id = ?2 AND v.to_id = ?3 AND v.type = ?4";
constexpr char ALL_SQL[] =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0"
    " ORDER BY v.from_id, v.type, v.to_id";
constexpr char LIST_SQL[] =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0"
    " AND ((?2 = 1 AND v.from_id = ?4) OR (?3 = 1 AND v.to_id = ?4)) AND (?5 = '' OR v.type = ?5)"
    " ORDER BY v.type, v.from_id, v.to_id";
constexpr char CHANGED_ENDS_SQL[] = "SELECT DISTINCT from_id, to_id FROM link_version WHERE cp_seq > ?1";
constexpr char WRITE_SQL[] =
    "INSERT INTO link_version (from_id, to_id, type, cp_seq, removed) VALUES (?1, ?2, ?3, ?4, ?5)"
    " ON CONFLICT(from_id, to_id, type, cp_seq) DO UPDATE SET removed = excluded.removed";

constexpr char ALLOWED_HELP[] =
    "허용되는 링크: interface satisfies requirement(functional) | design satisfies requirement/interface | "
    "unit_test verifies design | system_test verifies requirement(functional) | e2e_test verifies interface | "
    "file(gui_draft/c_header) implements interface | file(source) implements design | "
    "milestone covers design/unit_test | design(module) depends_on design(module)";

constexpr KiottySpecLinkType ANY_TYPE = static_cast<KiottySpecLinkType>(0);

LinkRecord readLink(const Row &row)
{
    LinkRecord record;
    record.fromId = std::string(viewOf(row.text(0)));
    record.toId = std::string(viewOf(row.text(1)));
    parse(viewOf(row.text(2)), record.type);
    record.cpSeq = row.integer(3);
    return record;
}

LinkListResult collect(Storage &storage, Query &query)
{
    std::vector<LinkRecord> links;
    auto stepped = query.step();
    while (stepped.value) {
        links.push_back(readLink(query.row()));
        stepped = query.step();
    }
    if (!stepped) { return LinkListResult::failure(storage.check(stepped.code)); }
    return LinkListResult::success(std::move(links));
}

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

void pushUnique(std::vector<std::string> &into, const std::string &value)
{
    const bool isPresent = std::find(into.begin(), into.end(), value) != into.end();
    if (!isPresent) { into.push_back(value); }
}

void append(std::vector<std::string> &into, std::vector<std::string> &from)
{
    for (std::string &value : from) { into.push_back(std::move(value)); }
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

bool isQuote(char c) { return c == '"' || c == '\''; }

size_t skipSpaces(std::string_view text, size_t position)
{
    while (position < text.size() && isSpace(text[position])) { ++position; }
    return position;
}

size_t quotedValueEnd(std::string_view text, size_t start)
{
    size_t end = start;
    while (end < text.size() && !isQuote(text[end])) { ++end; }
    return end;
}

std::string baseId(std::string_view id)
{
    const size_t dot = id.find('.');
    return std::string(id.substr(0, dot));
}

std::set<std::string> markedIds(std::string_view content)
{
    std::set<std::string> found;
    size_t position = content.find(GUI_ID_ATTRIBUTE);
    while (position != std::string_view::npos) {
        size_t next = position + 1;
        const size_t equals = skipSpaces(content, position + GUI_ID_ATTRIBUTE.size());
        const bool hasEquals = equals < content.size() && content[equals] == '=';
        const size_t quote = hasEquals ? skipSpaces(content, equals + 1) : content.size();
        const bool hasQuote = quote < content.size() && isQuote(content[quote]);
        const size_t end = hasQuote ? quotedValueEnd(content, quote + 1) : content.size();
        const bool isClosed = end < content.size() && end > quote + 1;
        if (isClosed) {
            found.insert(baseId(content.substr(quote + 1, end - quote - 1)));
            next = end + 1;
        }
        position = content.find(GUI_ID_ATTRIBUTE, next);
    }
    return found;
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

LinkListResult LinkStore::all(int64_t cut)
{
    auto query = _storage.prepare(literalText(ALL_SQL));
    if (!query) { return LinkListResult::failure(query.code); }
    query.value.bind(1, cut);
    return collect(_storage, query.value);
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
    return collect(_storage, query.value);
}

TextListResult LinkStore::targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut)
{
    auto links = list(fromId, KIOTTY_SPEC_DIRECTION_UPSTREAM, type, cut);
    if (!links) { return TextListResult::failure(links.code); }
    std::vector<std::string> ids;
    for (LinkRecord &link : links.value) { ids.push_back(std::move(link.toId)); }
    return TextListResult::success(std::move(ids));
}

TextListResult LinkStore::sources(std::string_view toId, KiottySpecLinkType type, int64_t cut)
{
    auto links = list(toId, KIOTTY_SPEC_DIRECTION_DOWNSTREAM, type, cut);
    if (!links) { return TextListResult::failure(links.code); }
    std::vector<std::string> ids;
    for (LinkRecord &link : links.value) { ids.push_back(std::move(link.fromId)); }
    return TextListResult::success(std::move(ids));
}

TextListResult LinkStore::traverse(std::string_view id, KiottySpecDirection dir, int32_t depth)
{
    const int32_t limit = depth <= 0 ? DEFAULT_TRAVERSE_DEPTH : depth;
    std::vector<std::string> result;
    std::set<std::string> seen{std::string(id)};
    std::deque<std::pair<std::string, int32_t>> queue{{std::string(id), 0}};
    while (!queue.empty()) {
        const std::pair<std::string, int32_t> current = queue.front();
        queue.pop_front();
        if (current.second >= limit) { continue; }
        auto next = neighbors(current.first, dir);
        if (!next) { return next; }
        for (std::string &node : next.value) {
            if (!seen.insert(node).second) { continue; }
            result.push_back(node);
            queue.emplace_back(std::move(node), current.second + 1);
        }
    }
    return TextListResult::success(std::move(result));
}

ItemListResult LinkStore::impactSince(int64_t cpSeq)
{
    const auto changed = changedIds(cpSeq);
    if (!changed) { return ItemListResult::failure(changed.code); }
    const auto affected = linkedIds(changed.value, cpSeq);
    if (!affected) { return ItemListResult::failure(affected.code); }
    std::vector<ItemRecord> result;
    for (const std::string &id : affected.value) {
        if (changed.value.count(id) != 0) { continue; }
        auto item = _items.find(id);
        if (!item) { return ItemListResult::failure(item.code); }
        if (item.value) { result.push_back(std::move(*item.value)); }
    }
    return ItemListResult::success(std::move(result));
}

ScopeResultResult LinkStore::scopeOf(std::string_view id)
{
    const auto touch = modulesOf(id);
    if (!touch) { return ScopeResultResult::failure(touch.code); }
    const std::vector<std::string> &modules = touch.value.modules;
    auto affected = dependents(modules);
    if (!affected) { return ScopeResultResult::failure(affected.code); }
    if (modules.size() > 1) { affected.value.insert(modules.begin(), modules.end()); }
    ScopeResult result;
    result.affectedModules.assign(affected.value.begin(), affected.value.end());
    const bool intra = modules.size() == 1 && affected.value.empty() && !touch.value.touchesNonModule;
    result.scope = intra ? KIOTTY_SPEC_SCOPE_INTRA_MODULE : KIOTTY_SPEC_SCOPE_INTER_MODULE;
    return ScopeResultResult::success(std::move(result));
}

TextListResult LinkStore::suspects(std::string_view failedTargetId)
{
    const auto target = _items.get(failedTargetId);
    if (!target) { return TextListResult::failure(target.code); }
    if (target.value.kind == KIOTTY_SPEC_ITEM_KIND_RULE) { return ruleSuspects(); }
    const auto verified = targets(failedTargetId, KIOTTY_SPEC_LINK_TYPE_VERIFIES);
    if (!verified) { return verified; }
    std::vector<std::string> designs;
    std::vector<std::string> result;
    for (const std::string &verifiedId : verified.value) {
        const KiottySpecStatus collected = suspectsOf(verifiedId, designs, result);
        if (collected != KIOTTY_SPEC_STATUS_SUCCESS) { return TextListResult::failure(collected); }
    }
    for (const std::string &design : designs) {
        pushUnique(result, design);
        const auto files = sources(design, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS);
        if (!files) { return files; }
        for (const std::string &file : files.value) { pushUnique(result, file); }
    }
    return TextListResult::success(std::move(result));
}

TextResult LinkStore::matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind)
{
    const auto rows = _items.ofKind(rowKind);
    if (!rows) { return TextResult::failure(rows.code); }
    const auto cols = _items.ofKind(colKind);
    if (!cols) { return TextResult::failure(cols.code); }
    const auto links = all();
    if (!links) { return TextResult::failure(links.code); }
    std::set<std::pair<std::string, std::string>> pairs;
    for (const LinkRecord &link : links.value) {
        pairs.emplace(link.fromId, link.toId);
        pairs.emplace(link.toId, link.fromId);
    }
    std::string csv = std::string(name(rowKind)) + "\\" + std::string(name(colKind));
    for (const ItemRecord &col : cols.value) { csv += "," + col.id; }
    csv += "\n";
    for (const ItemRecord &row : rows.value) {
        csv += row.id;
        for (const ItemRecord &col : cols.value) { csv += pairs.count({row.id, col.id}) != 0 ? ",x" : ","; }
        csv += "\n";
    }
    return TextResult::success(std::move(csv));
}

GuiScanResultResult LinkStore::scanGuiDraft(std::string_view fileId)
{
    const auto file = _files.get(fileId);
    if (!file) { return GuiScanResultResult::failure(file.code); }
    if (file.value.role != KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT) { return GuiScanResultResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("GUI 초안 파일이 아님: "), spanOf(file.value.id)})); }
    const auto content = _files.read(file.value);
    if (!content) { return GuiScanResultResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 읽을 수 없음: "), spanOf(file.value.path)})); }
    const std::set<std::string> found = markedIds(*content);
    GuiScanResult result;
    for (const std::string &id : found) {
        const auto item = _items.find(id);
        if (!item) { return GuiScanResultResult::failure(item.code); }
        const bool isInterface = item.value && item.value->kind == KIOTTY_SPEC_ITEM_KIND_INTERFACE;
        if (!isInterface) { result.unknownIds.push_back(id); }
    }
    const auto views = targets(file.value.id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS);
    if (!views) { return GuiScanResultResult::failure(views.code); }
    for (const std::string &viewId : views.value) {
        if (found.count(viewId) != 0) { continue; }
        auto view = _items.find(viewId);
        if (!view) { return GuiScanResultResult::failure(view.code); }
        if (view.value) { result.missingViews.push_back(std::move(*view.value)); }
    }
    return GuiScanResultResult::success(std::move(result));
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

TextListResult LinkStore::neighbors(std::string_view id, KiottySpecDirection dir)
{
    std::vector<std::string> next;
    if (dir != KIOTTY_SPEC_DIRECTION_DOWNSTREAM) {
        auto up = targets(id, ANY_TYPE);
        if (!up) { return up; }
        append(next, up.value);
    }
    if (dir != KIOTTY_SPEC_DIRECTION_UPSTREAM) {
        auto down = sources(id, ANY_TYPE);
        if (!down) { return down; }
        append(next, down.value);
    }
    return TextListResult::success(std::move(next));
}

Result<std::set<std::string>, KiottySpecStatus> LinkStore::changedIds(int64_t cpSeq)
{
    using IdSetResult = Result<std::set<std::string>, KiottySpecStatus>;
    const auto items = _items.changedBetween(cpSeq, kCurrent);
    if (!items) { return IdSetResult::failure(items.code); }
    const auto files = _files.changedSince(cpSeq);
    if (!files) { return IdSetResult::failure(files.code); }
    std::set<std::string> changed;
    for (const ItemChange &change : items.value) { changed.insert(change.record.id); }
    for (const FileRecord &file : files.value) { changed.insert(file.id); }
    return IdSetResult::success(std::move(changed));
}

Result<std::set<std::string>, KiottySpecStatus> LinkStore::linkedIds(const std::set<std::string> &changed, int64_t cpSeq)
{
    using IdSetResult = Result<std::set<std::string>, KiottySpecStatus>;
    auto query = _storage.prepare(literalText(CHANGED_ENDS_SQL));
    if (!query) { return IdSetResult::failure(query.code); }
    query.value.bind(1, cpSeq);
    std::set<std::string> affected;
    auto stepped = query.value.step();
    while (stepped.value) {
        affected.insert(std::string(viewOf(query.value.row().text(0))));
        affected.insert(std::string(viewOf(query.value.row().text(1))));
        stepped = query.value.step();
    }
    if (!stepped) { return IdSetResult::failure(_storage.check(stepped.code)); }
    for (const std::string &id : changed) {
        const auto links = list(id, KIOTTY_SPEC_DIRECTION_BOTH, ANY_TYPE);
        if (!links) { return IdSetResult::failure(links.code); }
        for (const LinkRecord &link : links.value) { affected.insert(link.fromId == id ? link.toId : link.fromId); }
    }
    return IdSetResult::success(std::move(affected));
}

Result<ModuleTouch, KiottySpecStatus> LinkStore::modulesOf(std::string_view id)
{
    using TouchResult = Result<ModuleTouch, KiottySpecStatus>;
    ModuleTouch touch;
    const auto item = _items.find(id);
    if (!item) { return TouchResult::failure(item.code); }
    std::vector<std::string> designs;
    if (item.value) {
        switch (item.value->kind) {
        case KIOTTY_SPEC_ITEM_KIND_DESIGN:
            designs.push_back(item.value->id);
            break;
        case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST: {
            auto verified = targets(item.value->id, KIOTTY_SPEC_LINK_TYPE_VERIFIES);
            if (!verified) { return TouchResult::failure(verified.code); }
            designs = std::move(verified.value);
            break;
        }
        case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT:
        case KIOTTY_SPEC_ITEM_KIND_RULE:
        case KIOTTY_SPEC_ITEM_KIND_INTERFACE:
        case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST:
        case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST:
        case KIOTTY_SPEC_ITEM_KIND_MILESTONE:
            touch.touchesNonModule = true;
            break;
        }
    }
    if (!item.value) {
        const auto file = _files.get(id);
        if (!file) { return TouchResult::failure(file.code); }
        auto implemented = targets(id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS);
        if (!implemented) { return TouchResult::failure(implemented.code); }
        for (std::string &target : implemented.value) {
            const auto targetItem = _items.find(target);
            if (!targetItem) { return TouchResult::failure(targetItem.code); }
            const bool isDesign = targetItem.value && targetItem.value->kind == KIOTTY_SPEC_ITEM_KIND_DESIGN;
            if (isDesign) { designs.push_back(std::move(target)); }
            if (!isDesign) { touch.touchesNonModule = true; }
        }
    }
    for (const std::string &design : designs) {
        const KiottySpecStatus added = addDesign(touch, design);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return TouchResult::failure(added); }
    }
    std::sort(touch.modules.begin(), touch.modules.end());
    touch.modules.erase(std::unique(touch.modules.begin(), touch.modules.end()), touch.modules.end());
    return TouchResult::success(std::move(touch));
}

KiottySpecStatus LinkStore::addDesign(ModuleTouch &touch, std::string_view designId)
{
    const auto design = _items.find(designId);
    if (!design) { return design.code; }
    if (!design.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (design.value->category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE) {
        touch.modules.push_back(design.value->id);
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    touch.touchesNonModule = true;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<std::set<std::string>, KiottySpecStatus> LinkStore::dependents(const std::vector<std::string> &modules)
{
    using IdSetResult = Result<std::set<std::string>, KiottySpecStatus>;
    const std::set<std::string> own(modules.begin(), modules.end());
    std::set<std::string> affected;
    std::deque<std::string> queue(modules.begin(), modules.end());
    while (!queue.empty()) {
        const std::string module = queue.front();
        queue.pop_front();
        const auto users = sources(module, KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON);
        if (!users) { return IdSetResult::failure(users.code); }
        for (const std::string &dependent : users.value) {
            const bool isNew = own.count(dependent) == 0 && affected.insert(dependent).second;
            if (isNew) { queue.push_back(dependent); }
        }
    }
    return IdSetResult::success(std::move(affected));
}

TextListResult LinkStore::ruleSuspects()
{
    const auto files = _files.list(KIOTTY_SPEC_FILE_ROLE_SOURCE);
    if (!files) { return TextListResult::failure(files.code); }
    std::vector<std::string> result;
    for (const FileRecord &file : files.value) { pushUnique(result, file.id); }
    return TextListResult::success(std::move(result));
}

KiottySpecStatus LinkStore::suspectsOf(std::string_view verifiedId, std::vector<std::string> &designs,
                                       std::vector<std::string> &result)
{
    const auto item = _items.find(verifiedId);
    if (!item) { return item.code; }
    if (!item.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (item.value->kind == KIOTTY_SPEC_ITEM_KIND_DESIGN) {
        pushUnique(designs, item.value->id);
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    const auto satisfying = sources(item.value->id, KIOTTY_SPEC_LINK_TYPE_SATISFIES);
    if (!satisfying) { return satisfying.code; }
    for (const std::string &design : satisfying.value) { pushUnique(designs, design); }
    if (item.value->kind != KIOTTY_SPEC_ITEM_KIND_INTERFACE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto files = sources(item.value->id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS);
    if (!files) { return files.code; }
    for (const std::string &file : files.value) { pushUnique(result, file); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
