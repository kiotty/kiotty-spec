// 뷰어가 보이는 문장 (DSN-116). 라이브러리에는 안내 문장이 없어 뷰어가 가진다 (REQ-055).

export const PURPOSE = [
  '1. 라이프사이클을 로컬 git 저장소 안에서 관리한다.',
  '   사양은 <루트>/.kiotty/spec.kisp 파일 하나 (줄 단위 텍스트, diff · merge 가 쉬움).',
  '   코드와 함께 커밋한다.',
  '2. AI 가 추적성을 관리하고 필요한 것만 쿼리로 가져온다.',
  '   요구사항 → 설계 → 테스트 → 검사 결과가 관계로 이어져 있어, 지금 일에 필요한 항목만 읽는다.',
  '3. 체크포인트 단위로 관리한다.',
  '   모든 쓰기는 한 단계의 체크포인트 안에서 하고, 사용자가 확정하면 그 시점이 고정된다.',
  '   - 단계마다 사람이 검토하고 승인한 뒤 다음으로 간다 (AI 가 혼자 앞서가지 않는다).',
  '   - 확정 조건이 빠진 연결(인수 조건 · 테스트 짝 · 검사 결과)을 막는다.',
  '   - 무엇이 언제 왜 바뀌었는지 체크포인트 사이 차이로 본다.',
  '   - 변경 요청은 필요한 단계부터만 다시 밟는다.',
].join('\n');

export const ROLES = [
  '- AI: 체크포인트 시작, 항목 작성, 구현, 테스트 · 검사 실행과 결과 기록.',
  '- 사용자: 체크포인트 확정 · 정리, 원격 수칙 올리기 · 편입 · 버리기.',
  '- 뷰어: 보기만 한다. 기록은 명령줄 도구 ks 로 쓴다.',
].join('\n');

export const STEPS = [
  { step: 1, name: '변경 요청 접수', short: '변경 요청', does: '무엇을 왜 바꿀지 받는다', kinds: 'CR' },
  { step: 2, name: '요구사항', short: '요구사항', does: '해야 할 일과 지킬 개발 수칙을 정한다', kinds: 'REQ, RULE, PRULE' },
  { step: 3, name: '외부 인터페이스', short: '외부 인터페이스', does: '밖에서 보이는 약속을 정한다', kinds: 'UI' },
  { step: 4, name: '설계', short: '설계', does: '어떻게 만들지 정한다', kinds: 'DSN' },
  { step: 5, name: '유닛 테스트 계획', short: '유닛 테스트', does: '설계 단위 검증 방법을 정한다', kinds: 'UT' },
  { step: 6, name: '개발 계획', short: '개발 계획', does: '구현 순서를 마일스톤으로 묶는다', kinds: 'MS' },
  { step: 7, name: '구현', short: '구현', does: '코드와 유닛 테스트를 쓰고 돌린다', kinds: 'UTR' },
  { step: 8, name: '시스템 테스트 계획', short: '시스템 테스트 계획', does: '요구사항을 실행으로 검증할 시나리오', kinds: 'ST' },
  { step: 9, name: '시스템 테스트 실행', short: '시스템 테스트', does: '시스템 테스트를 돌리고 결과를 남긴다', kinds: 'STR' },
  { step: 10, name: '코드 검사', short: '코드 검사', does: '코드가 수칙을 지키는지 수칙마다 판정', kinds: 'CIR' },
  { step: 11, name: 'E2E 테스트 계획', short: 'E2E 계획', does: '외부 인터페이스를 끝까지 쓰는 시나리오', kinds: 'E2E' },
  { step: 12, name: 'E2E 테스트 실행', short: 'E2E 실행', does: 'E2E 를 돌리고 변경 요청을 닫는다', kinds: 'E2ER' },
];

export const CONDITION_REASONS = {
  ENTRY_STEP: '진입 단계 없음',
  ACCEPTANCE_CRITERIA: '인수 조건 없음',
  MILESTONE_COVERAGE: '마일스톤에 없음',
  SYSTEM_TEST_PAIR: '시스템 테스트 없음',
  END_TO_END_TEST_PAIR: 'E2E 테스트 없음',
  CODE_INSPECTION_PAIR: '검사 결과 없음',
  SINGLE_TARGET: '대상이 하나가 아님',
  PASSING_RESULT: 'PASS 아님',
  CLOSED_CHANGE_REQUEST: '열린 변경 요청',
};

export const STATE_NAMES = { started: '시작', running: '진행 중', done: '확정' };

export const TEXT = {
  brand: 'kiotty-spec',
  brandSub: '뷰어',
  openOther: '다른 파일 열기',
  tabs: { overview: '개요', progress: '진행', items: '항목', changes: '변경 요청' },
  loading: '불러오는 중',
  firstHint: '처음이면 개요 에서 이 기록이 무엇인지 보세요.',
  noProgress: '진행 기록이 없습니다',
  noProgressHint: '체크포인트가 아직 없습니다. 다른 탭은 그대로 볼 수 있습니다.',
  waitingUser: '사용자 확정을 기다립니다',
  waitingNext: '다음 단계를 기다립니다',
  unmetTitle: '작업할 항목',
  checkpointsTitle: '체크포인트',
  everyStep: '모든 단계',
  noCheckpointInStep: '이 단계의 체크포인트가 없습니다',
  createdAt: '만든 시각',
  itemCount: '항목',
  checkpointCount: '체크포인트',
  purpose: '목적',
  roles: '역할',
  steps: '12 단계',
  counts: '항목 수',
  openRepoTitle: '공개 저장소에서 열기',
  repoLabel: '저장소',
  refLabel: '버전',
  refPlaceholder: '태그 · 브랜치 · commit (비우면 기본 브랜치)',
  pathLabel: '파일 위치',
  openButton: '열기',
  repoFormat: 'OWNER/NAME 꼴로 입력하세요',
  openLocalTitle: '내 PC 의 파일 열기',
  localSoon: '로컬 파일 열기는 다음 버전에서 됩니다.',
  localPrivacy: '파일은 브라우저 안에서만 읽고 어디에도 보내지 않습니다.',
  retry: '다시 시도',
  unsupported: '이 브라우저에서는 열 수 없습니다',
  supported: 'Windows · macOS 의 크롬 · 엣지 · 사파리 최신 두 주 버전에서 열립니다.',
  notReady: '이 화면은 아직 준비 중입니다.',
  localHint: '비공개 저장소라면 아래에서 로컬 파일로 여세요.',
};

// 열기 실패 문구 (UI-169 오류 표시). error 는 {code, repo, ref, path} 또는 SpecError.
export function describeOpenError(error, link) {
  const repo = link?.repo ?? '';
  const ref = link?.ref ?? '기본 브랜치';
  const path = link?.path ?? '';
  switch (error.code) {
    case 'REPO_NOT_FOUND': return { text: `저장소 ${repo} 을 찾지 못했습니다 (없거나 비공개)`, local: true };
    case 'REF_NOT_FOUND': return { text: `버전 ${ref} 를 찾지 못했습니다`, local: true };
    case 'PATH_NOT_FOUND': return { text: `${ref} 에 ${path} 가 없습니다`, local: true };
    case 'NOT_FOUND': return { text: `${repo} @ ${ref} 의 ${path} 를 찾지 못했습니다`, local: true };
    case 'NETWORK': return { text: '파일을 받지 못했습니다', retry: true };
    case 'TOO_LARGE': return { text: '기록이 뷰어 용량보다 큽니다' };
    case 'MODEL': return { text: '이 뷰어가 모르는 모델 버전입니다' };
    case 'FORMAT': return { text: error.line ? `사양 파일 형식이 아닙니다 (${error.line} 번째 줄)` : '사양 파일 형식이 아닙니다' };
    case 'UNSUPPORTED': return { text: TEXT.unsupported };
    default: return { text: `열 수 없습니다: ${error.message ?? error.code}` };
  }
}
