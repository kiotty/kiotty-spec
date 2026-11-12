#include "implementation/domain/entity/kiotty_item_id.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;

using entity::ItemKind;

namespace {

using Failure = std::function<bool()>;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

impl::Span<const char> textOf(const char* text)
{
    return impl::Span<const char>(text, std::strlen(text));
}

const impl::ModelCatalog& catalog()
{
    static impl::ModelCatalog loaded;
    static const KiottySpecStatus status = loaded.load();
    (void)status;
    return loaded;
}

bool testRecordSizes()
{
    bool ok = true;
    ok = expectTrue(sizeof(entity::ItemRecord) == 424, "item record 424") && ok;
    ok = expectTrue(sizeof(entity::TagRecord) == 24, "tag record 24") && ok;
    ok = expectTrue(sizeof(entity::RelationRecord) == 24, "relation record 24") && ok;
    ok = expectTrue(sizeof(entity::CheckpointRecord) == 88, "checkpoint record 88") && ok;
    ok = expectTrue(sizeof(entity::DecisionRecord) == 2064, "decision record 2064") && ok;
    ok = expectTrue(sizeof(entity::RelationDefinition) == 72, "relation definition 72") && ok;
    return ok;
}

bool testItemId()
{
    bool ok = true;
    ok = expectTrue(entity::validateItemId(textOf("DSN-004")) == KIOTTY_SPEC_STATUS_SUCCESS, "hyphen id") && ok;
    ok = expectTrue(entity::validateItemId(textOf("REQ_003")) == KIOTTY_SPEC_STATUS_SUCCESS, "underscore id") && ok;
    ok = expectTrue(entity::validateItemId(textOf("9a")) == KIOTTY_SPEC_STATUS_SUCCESS, "digit first") && ok;
    ok = expectTrue(entity::validateItemId(textOf("ABCDEFGHIJKLMNO")) == KIOTTY_SPEC_STATUS_SUCCESS, "15 bytes fit") && ok;
    ok = expectTrue(entity::validateItemId(textOf("ABCDEFGHIJKLMNOP")) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "16 bytes exceed") && ok;
    ok = expectTrue(entity::validateItemId(textOf("")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "empty id") && ok;
    ok = expectTrue(entity::validateItemId(textOf("_A_")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "underscore first") && ok;
    ok = expectTrue(entity::validateItemId(textOf("-A")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "hyphen first") && ok;
    ok = expectTrue(entity::validateItemId(textOf("A B")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "space") && ok;
    ok = expectTrue(entity::validateItemId(textOf("A*B")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "markdown emphasis") && ok;
    ok = expectTrue(entity::validateItemId(textOf("A[1]")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "markdown link") && ok;
    ok = expectTrue(entity::validateItemId(textOf("\xEC\x9A\x94")) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "non ascii") && ok;
    return ok;
}

bool testModels()
{
    const impl::ModelCatalog& models = catalog();
    bool ok = true;
    ok = expectTrue(std::strcmp(models.model(ItemKind::DESIGN).name.terminated(), "설계 사항") == 0, "design name") && ok;
    ok = expectTrue(std::strcmp(models.model(ItemKind::END_TO_END_TEST_RESULT).id_prefix.terminated(), "E2ER") == 0, "e2e result prefix") && ok;
    ok = expectTrue(models.model(ItemKind::CODE_INSPECTION_RESULT).created_in == entity::Step::CODE_INSPECTING, "inspection step") && ok;
    ok = expectTrue(models.model(ItemKind::CHANGE_REQUEST).tags.length() == 3, "change request tags") && ok;
    ok = expectTrue(models.model(ItemKind::MILESTONE).tags.length() == 0, "milestone has no tags") && ok;
    ok = expectTrue(models.model(ItemKind::CHANGE_REQUEST).relations.length() == 5, "change request relations") && ok;
    return ok;
}

bool testTagLookup()
{
    const impl::ModelCatalog& models = catalog();
    bool ok = true;
    const auto state = models.finder().findTag(ItemKind::DESIGN, textOf("구현 상태"));
    ok = expectTrue(static_cast<bool>(state) && state.value == 1, "design state tag index") && ok;
    const auto done = models.finder().findTagBit(ItemKind::DESIGN, state.value, textOf("구현 완료"));
    ok = expectTrue(static_cast<bool>(done) && done.value == 2, "done bit") && ok;
    ok = expectTrue(models.finder().findTag(ItemKind::MILESTONE, textOf("구현 상태")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "milestone tag missing") && ok;
    const auto entry = models.finder().findTag(ItemKind::CHANGE_REQUEST, textOf("진입 단계"));
    const auto step12 = models.finder().findTagBit(ItemKind::CHANGE_REQUEST, entry.value, textOf("Step 12"));
    ok = expectTrue(static_cast<bool>(step12) && step12.value == 11, "entry step 12 bit") && ok;
    return ok;
}

bool testTagValue()
{
    const impl::ModelCatalog& models = catalog();
    bool ok = true;
    ok = expectTrue(models.checkTagValue(ItemKind::REQUIREMENT, 0, 0b011) == KIOTTY_SPEC_STATUS_SUCCESS, "many bits on many tag") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::REQUIREMENT, 0, 0) == KIOTTY_SPEC_STATUS_SUCCESS, "zero clears") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::REQUIREMENT, 0, 0b1000) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "unnamed bit") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::REQUIREMENT, 1, 1) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "tag index out of model") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::DESIGN, 1, 0b100) == KIOTTY_SPEC_STATUS_SUCCESS, "single bit") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::DESIGN, 1, 0b101) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "two bits on single tag") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::DESIGN, 0, uint64_t{1} << 48) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "bit above 48") && ok;
    ok = expectTrue(models.checkTagValue(ItemKind::MILESTONE, 0, 1) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "model without tags") && ok;
    return ok;
}

bool testRelations()
{
    const impl::ModelCatalog& models = catalog();
    bool ok = true;
    const auto related = models.finder().findRelation(ItemKind::REQUIREMENT, textOf("관련 요구사항"));
    ok = expectTrue(static_cast<bool>(related) && related.value == 0, "requirement self relation") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::REQUIREMENT, 0, ItemKind::REQUIREMENT) == KIOTTY_SPEC_STATUS_SUCCESS, "requirement to requirement") && ok;
    const auto depends = models.finder().findRelation(ItemKind::DESIGN, textOf("의존 설계 사항"));
    ok = expectTrue(static_cast<bool>(depends) && depends.value == 2, "design dependency index") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::DESIGN, 1, ItemKind::EXTERNAL_INTERFACE) == KIOTTY_SPEC_STATUS_SUCCESS, "design to interface") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::DESIGN, 1, ItemKind::REQUIREMENT) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "wrong target kind") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::DESIGN, 3, ItemKind::DESIGN) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "relation index out of model") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::CHANGE_REQUEST, 4, ItemKind::MILESTONE) == KIOTTY_SPEC_STATUS_SUCCESS, "change request milestone") && ok;
    ok = expectTrue(models.checkRelation(ItemKind::CHANGE_REQUEST, 0, ItemKind::UNIT_TEST_PLAN) == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "change request does not touch outputs") && ok;
    const auto inspected = models.finder().findRelation(ItemKind::CODE_INSPECTION_RESULT, textOf("검사 개발 수칙"));
    ok = expectTrue(static_cast<bool>(inspected) && models.model(ItemKind::CODE_INSPECTION_RESULT).relations[inspected.value].cardinality == entity::Cardinality::SINGLE, "inspection is one to one") && ok;
    return ok;
}


}

int main()
{
    if (catalog().model(ItemKind::DESIGN).kind != ItemKind::DESIGN) {
        std::printf("FAIL catalog load\n");
        return 1;
    }

    const std::pair<const char*, Failure> tests[] = {
        {"record sizes", testRecordSizes},
        {"item id", testItemId},
        {"models", testModels},
        {"tag lookup", testTagLookup},
        {"tag value", testTagValue},
        {"relations", testRelations},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool ok = test.second();
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.first);
        if (!ok) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
