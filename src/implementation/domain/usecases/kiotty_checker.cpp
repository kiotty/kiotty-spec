#include "implementation/domain/usecases/kiotty_checker.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::RelationRecord;

constexpr uint32_t MISSING = 0xFFFFFFFFu;

bool startsWithKeyword(std::string_view text)
{
    for (const std::string_view keyword : ACCEPTANCE_CRITERIA_KEYWORDS) {
        const size_t position = text.find(keyword);
        if (position != std::string_view::npos) { return true; }
    }
    const size_t text_size = text.size();
    for (size_t index = 0; index + 10 <= text_size; ++index) {
        bool same = true;
        const char word[] = "acceptance";
        for (size_t offset = 0; offset < 10; ++offset) {
            char letter = text[index + offset];
            if (letter >= 'A' && letter <= 'Z') { letter = static_cast<char>(letter - 'A' + 'a'); }
            if (letter != word[offset]) {
                same = false;
                break;
            }
        }
        if (same) { return true; }
    }
    return false;
}

bool isListLine(std::string_view line)
{
    const size_t line_size = line.size();
    size_t index = 0;
    while (index < line_size && (line[index] == ' ' || line[index] == '\t')) { ++index; }
    if (index + 1 < line_size &&
        (line[index] == '-' || line[index] == '*' || line[index] == '+') &&
        line[index + 1] == ' ') {
        return true;
    }
    size_t digits = index;
    while (digits < line_size && line[digits] >= '0' && line[digits] <= '9') { ++digits; }
    return digits > index && digits + 1 < line_size &&
           (line[digits] == '.' || line[digits] == ')') && line[digits + 1] == ' ';
}

}

Span<const UnmetCondition> Checker::collect(entity::Step step)
{
    _length = 0;
    switch (step) {
    case entity::Step::CHANGE_REQUEST_REPORTED: checkEntrySteps(); break;
    case entity::Step::REQUIREMENT_DISCUSSING: checkAcceptanceCriteria(); break;
    case entity::Step::DEVELOPMENT_PLAN_DISCUSSING: checkMilestoneCoverage(); break;
    case entity::Step::SYSTEM_TEST_PLAN_DISCUSSING:
        checkOneToOne(ItemKind::REQUIREMENT, ItemKind::SYSTEM_TEST_PLAN,
                      SYSTEM_TEST_REQUIREMENT_RELATION, KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR,
                      true);
        break;
    case entity::Step::CODE_INSPECTING:
        checkOneToOne(ItemKind::RULE, ItemKind::CODE_INSPECTION_RESULT,
                      CODE_INSPECTION_RULE_RELATION, KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR,
                      false);
        break;
    case entity::Step::END_TO_END_TEST_DISCUSSING:
        checkOneToOne(ItemKind::EXTERNAL_INTERFACE, ItemKind::END_TO_END_TEST_PLAN,
                      END_TO_END_TEST_INTERFACE_RELATION,
                      KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR, false);
        break;
    case entity::Step::END_TO_END_TEST_RUN: checkResults(); break;
    case entity::Step::EXTERNAL_INTERFACE_DISCUSSING:
    case entity::Step::DESIGN_DISCUSSING:
    case entity::Step::UNIT_TEST_PLAN_DISCUSSING:
    case entity::Step::IMPLEMENTING:
    case entity::Step::SYSTEM_TEST_RUN: break;
    }
    return Span<const UnmetCondition>(_rows.data(), _length);
}

TagBit Checker::tagBit(ItemKind kind, std::string_view tag, std::string_view bit)
{
    const auto found = _w.catalog.finder().findTag(kind, spanOf(tag));
    if (!found) { return TagBit{}; }
    const auto position = _w.catalog.finder().findTagBit(kind, found.value, spanOf(bit));
    if (!position) { return TagBit{}; }
    return TagBit{uint64_t{1} << position.value, found.value};
}

uint32_t Checker::relationIndex(ItemKind kind, std::string_view name)
{
    const auto found = _w.catalog.finder().findRelation(kind, spanOf(name));
    return found ? found.value : MISSING;
}

size_t Checker::countRelations(ItemKind model, uint32_t relation, ItemHandle item, bool as_target)
{
    size_t count = 0;
    for (const RelationRecord& record : _w.relations.all()) {
        if (record.model != model || record.relation != relation) { continue; }
        const ItemHandle side = as_target ? record.to : record.from;
        if (side.index == item.index) { ++count; }
    }
    return count;
}

bool Checker::isOpenChangeRequest(ItemHandle handle)
{
    const TagBit open =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_OPEN_BIT);
    const TagBit in_progress =
        tagBit(ItemKind::CHANGE_REQUEST, CHANGE_REQUEST_STATE_TAG, CHANGE_REQUEST_IN_PROGRESS_BIT);
    if (open.tag_index == MISSING) { return true; }
    const uint64_t value = _w.items.tags().value(handle, open.tag_index);
    return value == 0 || (value & (open.bit | in_progress.bit)) != 0;
}

size_t Checker::openChangeRequestCount()
{
    size_t count = 0;
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (is_open) { ++count; }
    }
    return count;
}

uint32_t Checker::earliestEntryStep()
{
    const auto entry = _w.catalog.finder().findTag(ItemKind::CHANGE_REQUEST,
                                                   spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    if (!entry) { return 0; }
    uint32_t earliest = 0;
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (!is_open) { continue; }
        const uint64_t value = _w.items.tags().value(handle, entry.value);
        for (uint32_t bit = 0; bit < entity::STEP_COUNT; ++bit) {
            if ((value & (uint64_t{1} << bit)) == 0) { continue; }
            if (earliest == 0 || bit + 1 < earliest) { earliest = bit + 1; }
            break;
        }
    }
    return earliest;
}

size_t Checker::countAcceptanceCriteria(Span<const char> body)
{
    const std::string_view text = viewOf(body);
    size_t count = 0;
    size_t active_level = 0;
    size_t start = 0;
    const size_t text_size = text.size();
    while (start <= text_size) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) { end = text_size; }
        std::string_view line = text.substr(start, end - start);
        const bool has_carriage_return = !line.empty() && line.back() == '\r';
        if (has_carriage_return) { line.remove_suffix(1); }
        const size_t line_size = line.size();
        size_t level = 0;
        while (level < line_size && line[level] == '#') { ++level; }
        if (level > 0 && level <= 6 && level < line_size && line[level] == ' ') {
            if (active_level != 0 && level <= active_level) { active_level = 0; }
            const bool is_keyword = startsWithKeyword(line.substr(level));
            if (is_keyword) { active_level = level; }
        } else if (active_level != 0) {
            const bool is_list = isListLine(line);
            if (is_list) { ++count; }
        }
        if (end == text_size) { break; }
        start = end + 1;
    }
    return count;
}

void Checker::checkEntrySteps()
{
    const auto entry = _w.catalog.finder().findTag(ItemKind::CHANGE_REQUEST,
                                                   spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (!is_open) { continue; }
        const uint64_t entry_step = entry ? _w.items.tags().value(handle, entry.value) : 0;
        if (entry_step != 0) { continue; }
        add(KIOTTY_SPEC_CONDITION_ENTRY_STEP, item.value->id);
    }
}

void Checker::checkAcceptanceCriteria()
{
    const TagBit functional =
        tagBit(ItemKind::REQUIREMENT, REQUIREMENT_KIND_TAG, FUNCTIONAL_REQUIREMENT_BIT);
    if (functional.tag_index == MISSING) { return; }
    std::vector<char>& buffer = _body;
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != ItemKind::REQUIREMENT) { continue; }
        const uint64_t kind_value = _w.items.tags().value(handle, functional.tag_index);
        if ((kind_value & functional.bit) == 0) { continue; }
        const auto length = _w.items.reader().readBody(handle, buffer.data(), buffer.size());
        const size_t criteria_count =
            length ? countAcceptanceCriteria(Span<const char>(buffer.data(), length.value)) : 0;
        if (criteria_count == 0) { add(KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA, item.value->id); }
    }
}

void Checker::checkMilestoneCoverage()
{
    const uint32_t designs = relationIndex(ItemKind::MILESTONE, MILESTONE_DESIGN_RELATION);
    const uint32_t tests = relationIndex(ItemKind::MILESTONE, MILESTONE_UNIT_TEST_RELATION);
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item) { continue; }
        uint32_t relation = MISSING;
        if (item.value->kind == ItemKind::DESIGN) { relation = designs; }
        if (item.value->kind == ItemKind::UNIT_TEST_PLAN) { relation = tests; }
        if (relation == MISSING) { continue; }
        const size_t count = countRelations(ItemKind::MILESTONE, relation, handle, true);
        if (count == 0) { add(KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE, item.value->id); }
    }
}

void Checker::checkOneToOne(ItemKind target_kind, ItemKind test_kind,
                            std::string_view relation_name, KiottySpecCondition pair_condition,
                            bool functional_only)
{
    const uint32_t relation = relationIndex(test_kind, relation_name);
    const TagBit functional = functional_only ? tagBit(ItemKind::REQUIREMENT, REQUIREMENT_KIND_TAG,
                                                       FUNCTIONAL_REQUIREMENT_BIT)
                                              : TagBit {};
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != target_kind) { continue; }
        const bool can_read_tag = functional_only && functional.tag_index != MISSING;
        const uint64_t kind_value =
            can_read_tag ? _w.items.tags().value(handle, functional.tag_index) : 0;
        if (functional_only && (kind_value & functional.bit) == 0) { continue; }
        const size_t test_count = countRelations(test_kind, relation, handle, true);
        if (test_count != 1) { add(pair_condition, item.value->id); }
    }
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != test_kind) { continue; }
        const size_t target_count = countRelations(test_kind, relation, handle, false);
        if (target_count != 1) { add(KIOTTY_SPEC_CONDITION_SINGLE_TARGET, item.value->id); }
    }
}

void Checker::checkResults()
{
    const ItemKind kinds[] = { ItemKind::SYSTEM_TEST_RESULT, ItemKind::END_TO_END_TEST_RESULT,
                               ItemKind::CODE_INSPECTION_RESULT };
    const size_t capacity = _w.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item) { continue; }
        for (const ItemKind kind : kinds) {
            if (item.value->kind != kind) { continue; }
            const TagBit pass = tagBit(kind, RESULT_TAG, PASS_BIT);
            const uint64_t result_value =
                pass.tag_index == MISSING ? 0 : _w.items.tags().value(handle, pass.tag_index);
            if (pass.tag_index == MISSING || result_value != pass.bit) {
                add(KIOTTY_SPEC_CONDITION_PASSING_RESULT, item.value->id);
            }
        }
    }
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _w.items.reader().handleAt(index);
        const auto item = _w.items.reader().get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST) { continue; }
        const bool is_open = isOpenChangeRequest(handle);
        if (is_open) { add(KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST, item.value->id); }
    }
}

void Checker::add(KiottySpecCondition condition, entity::IdText id)
{
    const size_t row_count = _rows.size();
    if (_length == row_count) { return; }
    _rows[_length] = UnmetCondition{id, condition};
    ++_length;
}

}
