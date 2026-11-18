// github_source (DSN-120): 링크의 repo · ref · path 로 공개 저장소의 사양 파일을 받는다. 링크 읽기 · 쓰기는 domain/entity/link.js.

import { isRepoName } from '../../domain/entity/link.js';

const RAW = 'https://raw.githubusercontent.com';
const API = 'https://api.github.com';

export class SourceError extends Error {
  constructor(code, link) {
    super(code);
    this.name = 'SourceError';
    this.code = code;
    this.link = link;
  }
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
      if (!isRepoName(link.repo)) throw new SourceError('REPO_NOT_FOUND', link);
      const ref = link.ref ?? await findDefaultBranch(link);
      const response = await get(`${RAW}/${link.repo}/${encodePath(ref)}/${encodePath(link.path)}`);
      if (!response) throw new SourceError('NETWORK', link);
      if (response.status === 404) throw new SourceError(await explainNotFound(link, ref), link);
      if (!response.ok) throw new SourceError('NETWORK', link);
      return new Uint8Array(await response.arrayBuffer());
    },
  };
}
