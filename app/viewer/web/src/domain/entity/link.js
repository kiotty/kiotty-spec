// 링크 (DSN-120, UI-169): 주소의 ?repo=&ref=&path= 를 읽고 쓴다. 화면과 github_source 가 함께 쓴다.

export const DEFAULT_PATH = '.kiotty/spec.kisp';
// GitHub 계정은 영문 · 숫자 · '-', 저장소는 영문 · 숫자 · '_' · '.' · '-' 이고 '.' 이나 '..' 일 수 없다.
const REPO_PATTERN = /^[A-Za-z0-9-]+\/(?!\.\.?$)[A-Za-z0-9_.-]+$/;

export function isRepoName(text) {
  return REPO_PATTERN.test(text ?? '');
}

// search: location.search 꼴 ("?repo=a/b&ref=v1"). repo 가 없으면 null.
export function parseLink(search) {
  const params = new URLSearchParams(search ?? '');
  const repo = (params.get('repo') ?? '').trim();
  if (!repo) return null;
  const ref = (params.get('ref') ?? '').trim();
  const path = (params.get('path') ?? '').trim().replace(/^\/+/, '');
  return { repo, ref: ref || undefined, path: path || DEFAULT_PATH };
}

// UI-170 의 열기: 입력값으로 링크의 search 부분을 만든다.
export function formatLink({ repo, ref, path }) {
  const params = new URLSearchParams();
  params.set('repo', repo.trim());
  if (ref && ref.trim()) params.set('ref', ref.trim());
  if (path && path.trim() && path.trim() !== DEFAULT_PATH) params.set('path', path.trim());
  return `?${params.toString()}`;
}
