## 책임
단계·항목·마일스톤·변경 요청별 입력을 바이트 한도 안의 Markdown 으로 묶는다. 넘치면 제목만, 더 넘치면 생략 수와 개별 조회 안내 (REQ-025). 할 일·상태 요약.

## 모듈 인터페이스
```cpp
namespace kiotty::base {
struct Stores {
    CheckpointStore &checkpoints;
    ItemStore &items;
    FileStore &files;
    LinkStore &links;
    MilestoneService &milestones;
    ChangeRequestStore &changes;
    DecisionStore &decisions;
    ResultStore &results;
    Checker &checker;
};
// AI 가 한 번의 질의로 받을 맥락을 토큰(바이트) 한도 안에서 Markdown 으로 묶는다.
class ContextBuilder {
public:
    explicit ContextBuilder(Stores stores) : s_(stores) {}
    std::string forStep(int32_t step, size_t maxBytes);
    std::string forItem(std::string_view itemId, int32_t depth, size_t maxBytes);
    std::string forMilestone(std::string_view milestoneId, size_t maxBytes);
    std::string forChangeRequest(std::string_view crId, size_t maxBytes);
    std::string rules();
    std::string pending();
    std::string status();
    std::string renderItem(const core::ItemRecord &item, bool full);
};
} // namespace kiotty::base
```

## 멤버 변수
- `Stores s_;`

## 의존 모듈
- DSN-022 base::ChangeRequestStore
- DSN-025 base::Checker
- DSN-017 base::CheckpointStore
- DSN-023 base::DecisionStore
- DSN-008 core::Error
- DSN-019 base::FileStore
- DSN-018 base::ItemStore
- DSN-020 base::LinkStore
- DSN-021 base::MilestoneService
- DSN-024 base::ResultStore
- DSN-009 core::text
