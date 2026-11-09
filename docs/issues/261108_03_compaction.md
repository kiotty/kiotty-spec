# 수정

## 목적

1. Token 절약
2. 지나치게 복잡한 절차 간략 화

## 문서 관리 기능 삭제

1. KiottySpec에서는 문서를 관리하지 않음.
2. 현재, External Interface의 결과로 .hpp와 .html이 만들어지고 있음. AI는 가지고 있는 Item을 Query하여 이것을 바탕으로 .hpp와 .html을 만드는 것.
- 그래서 사용자가 .hpp와 .html의 수정 요청이 들어오면 아래와 같은 workflow로 이루어져야 함.

    사용자가 external interface 만들거나 수정해달라고 요청 -> 
    AI가 KiottySpec에 ExternalInterface Item 생성 또는 수정 -> 
    AI가 ExternalInterface Item을 Query하여 불러옴 ->
    Query 결과를 바탕으로 .hpp, .html을 생성
    
KiottySpec이 역으로 .hpp와 .html을 분석하지 않는다.

## Query 결과로 MD 문서가 나오는 기능 완전 삭제

## 사용자가 따로 묻지 않으면 연관 Item은 무조건 ID만 출력하고, 무조건 깊이는 1로 제약해야 함.

## Meta 데이터 관리

### 모델 시스템

1. 각 Item은 공통적으로 사용되는 Attribute들과 자신이 속한 Kind에 따른 Attribute가 정의된다.
2. Kind는 아래와 같다.

```cpp
KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST // 변경 요청 (버그, 외관 변경, 새 요구사항 등.. (Change Request도 Item으로 합병)
KIOTTY_SPEC_ITEM_KIND_REQUIREMENT // 요구사항
KIOTTY_SPEC_ITEM_KIND_RULE // 개발 수칙
KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE // 외부 인터페이스
KIOTTY_SPEC_ITEM_KIND_DESIGN // 설계 사항
KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN // 유닛 테스트
KIOTTY_SPEC_ITEM_KIND_MILESTONE // 마일 스톤
KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_RESULT // 유닛 테스트 결과
KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN // 시스템 테스트
KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_RESULT // 시스템 테스트 결과
KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN // E2E 테스트
KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_RESULT // E2E 테스트 결과
```

3. Item의 식별자는 '4byte generation, 4byte index'의 handle로 관리된다.
 
4. 공통적으로 사용되는 Attribute는 아래와 같다.

- ID[16byte]
    - 사용자에게 보여지는 식별자
- Kind[4byte](Kind String[32byte])
    - 종류
- Title[128byte] // 한글 40글자 이내
    - 제목
- Summary[256byte] // 한글 80글자 이내
    - 간략한 설명
- 현재 Checkpoint [8byte]
    - Handle
    - 현재 몇 번째 단계인지는 Checkpoint의 4byte sequencen number로 확인함.
- Patient Reference [8byte]
    - Handle
    - Handle에서 ID를 가져올 수 있음

5. Kind마다 Model을 가지고 있으며 Model은 아래의 정보를 가지고 있다.

- Name[32byte] // 한글 10글자 이내
    - Model의 이름. Kind에 대응되는 이름을 여기서 찾으면 될듯
- Tag Defintion들 (정의 가능한 Tag 개수, 최대 256개)
- Relation Definition들 (자기와 관련 있는 다른 Model들)

6. Tag Definition은 Or bitmap 검색을 위해 사용한다. Tag의 정의는 아래와 같다.

- Tag 이름 [64byte]
- n(0 ~ 47)번째 bit에 대응되는 Tag의 이름들 [64byte] * 48개

> 예시
>  Tag 이름 : "요구사항 종류"
>   0번 : "기능 요구사항"
>   1번 : "품질 요구사항"
>   2번 : "환경 요구사항"  


7. Relation Definition은 모델과 모델과의 연관 관계를 정의한 것이다. 그리고 이것이 Link 기능을 대체한다.

- Relation 이름 [64byte]
- 참조하고자 하는 Model(Kind) [4byte] 

> 이 관계가 어떤 참조인지 말해주기 때문에 기존의 LinkType은 삭제한다.

따라서, 기존의 Link Table의 record는 아래와 같다.

- Model (4byte)
    Model 종류
- Relation (4byte)
    Relation Handle Type
- from (8byte)
    - Item Handle
- to (8byte)
    - Item Handle

8. Model 별 별도로 정의되어야할 Field는 어떻게 해야할지 아직 아이디어가 없음
- 단 파일로서 관리되기보단 고정 크기가 제한된 Field들 (Tags, Relations, 크기가 너무 크지 않은 텍스트)들을 두어서
  AI가 기록을 읽을 때 너무 많은 토큰을 소모하지만 않으면 됨.


## 결정 기록 record 수정

- kind[4byte](Kind String[32byte])
    - 종류
- checkpoint[8byte]
    - 이 결정 기록이 일어났을 때의 체크포인트
- description[2048byte]
    - 어떤 결정을 했는지에 대한 기록. Token 절약과 메모리 절약을 위해 2048byte를 넘지 않는다.   



## Step enum 정의

typedef enum KiottySpecStep
{
    KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED = 1, // 사용자의 변경요청(버그, 외관변경, 기능(초기 앱))을 저장
    KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING = 2, // 요구사항 도출 및 변경
    KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING = 3,
    KIOTTY_SPEC_STEP_DESIGN_DISCUSSING = 4,
    KIOTTY_SPEC_STEP_UNIT_TEST_PLAN_DISCUSSING = 5,
    KIOTTY_SPEC_STEP_DEVELOPMNET_PLAN_DISCUSSING = 6,
    KIOTTY_SPEC_STEP_IMPLEMENTING = 7,
    KIOTTY_SPEC_STEP_SYSTEM_TEST_PLAN_DISCUSSING = 8,
    KIOTTY_SPEC_STEP_SYSTEM_TEST_RUN = 9,
    KIOTTY_SPEC_STEP_END_TO_END_TEST_DISCUSSING = 10,
    KIOTTY_SPEC_STEP_END_TO_END_TEST_RUN = 11,
};
    
## Checkpoint의 식별자

- '4byte reserved, 4byte sequence_number'의 handle로 관리함

## Checkpoint record 수정

- step[4byte]
    현재 무슨 Step인지
- state[4byte]
    STARTED(현재 Checkpoint를 시작함) -> RUNNING (현재 Checkpoint 진행 또는 재개함) -> DONE (사용자가 현재 체크포인트를 완료함)
- createdAt [8byte]
    언제 시작했는지 날짜
- description [64byte]
    현재, Checkpoint에 대한 간략한 설명
- previous[8byte]
    이전 CheckPoint

    

             
 