#include "implementation/domain/entity/body_template.h"

namespace kiotty::spec::implementation {

std::string bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category) {
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY) {
            return "## 진술\n\n## 지표\n\n## 목표값\n\n## 측정 방법\n";
        }
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT) {
            return "## 진술\n\n## 대상 (OS / 언어 / 하드웨어)\n";
        }
        return "## 진술\n<한 문장>\n\n## 근거 시나리오\n<요약>\n\n## 인수 조건\n"
               "- 전제 조건: ... / 행동: ... / 기대 결과: ...\n";
    case KIOTTY_SPEC_ITEM_KIND_RULE:
        return "## 규칙\n\n## 이유\n\n## 검사 방법\n";
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW) {
            return "## 목적\n\n## 진입 경로\n\n## 표시 데이터\n\n## 상호작용\n- \n\n## 표시 상태\n- 빈 화면:\n- 로딩:\n- 에러:\n";
        }
        return "## 시그니처\n```c\n```\n\n## 사전조건\n\n## 사후조건\n\n## 에러 코드\n\n## 소유권\n";
    case KIOTTY_SPEC_ITEM_KIND_DESIGN:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW) {
            return "## 참여 모듈\n\n## 순서\n1. \n\n## 데이터 흐름\n";
        }
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE) {
            return "## 책임\n\n## 모듈 인터페이스\n```cpp\n```\n\n## 멤버 변수\n\n## 의존 모듈\n";
        }
        return "## 내용\n";
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST:
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST:
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST:
        return "## 전제 조건\n\n## 행동\n\n## 기대 결과\n";
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE:
        return "## 목표\n\n## 완료 기준\n";
    }
    return "";
}

}
