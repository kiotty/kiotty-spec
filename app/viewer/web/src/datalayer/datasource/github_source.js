// github_source (DSN-120): 링크의 repo · ref · path 로 공개 저장소의 사양 파일을 받는다.

export const DEFAULT_PATH = '.kiotty/spec.kisp';
const RAW = 'https://raw.githubusercontent.com';
const API = 'https://api.github.com';
const REPO_PATTERN = /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/;

export class SourceError extends Error {
  constructor(code, link) {
    super(code);
    this.name = 'SourceError';
    this.code = code;
    this.link = link;
  }
}

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

function encodePath(text) {
  return text.split('/').map(encodeURIComponent).join('/');
}

export function createGithubSource(fetchFn = globalThis.fetch.bind(globalThis)) {
  async function get(url) {
    try {
      return await fetchFn(url);
    } catch {
      return null;
    }
  }

  async function findDefaultBranch(link) {
    const response = await get(`${API}/repos/${link.repo}`);
    if (!response) throw new SourceError('NETWORK', link);
    if (response.status === 404) throw new SourceError('REPO_NOT_FOUND', link);
    if (!response.ok) return 'HEAD';
    const body = await response.json();
    return body.default_branch || 'HEAD';
  }

  // raw 가 404 일 때만 API 로 원인을 가린다 (인증 없는 API 는 시간당 60 번).
  async function explainNotFound(link, ref) {
    const repo = await get(`${API}/repos/${link.repo}`);
    if (!repo || !(repo.ok || repo.status === 404)) return 'NOT_FOUND';
    if (repo.status === 404) return 'REPO_NOT_FOUND';
    const commit = await get(`${API}/repos/${link.repo}/commits/${encodeURIComponent(ref)}`);
    if (!commit || !(commit.ok || commit.status === 404 || commit.status === 422)) return 'NOT_FOUND';
    if (!commit.ok) return 'REF_NOT_FOUND';
    return 'PATH_NOT_FOUND';
  }

  return {
    async fetchSpec(link) {
      const ref = link.ref ?? await findDefaultBranch(link);
      const response = await get(`${RAW}/${link.repo}/${encodePath(ref)}/${encodePath(link.path)}`);
      if (!response) throw new SourceError('NETWORK', link);
      if (response.status === 404) throw new SourceError(await explainNotFound(link, ref), link);
      if (!response.ok) throw new SourceError('NETWORK', link);
      return new Uint8Array(await response.arrayBuffer());
    },
  };
}
