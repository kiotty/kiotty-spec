// UT-112 link parsing, UT-113 fetch and 404 cause
import test from 'node:test';
import assert from 'node:assert/strict';
import { createGithubSource, formatLink, isRepoName, parseLink } from '../../../app/viewer/web/src/datalayer/datasource/github_source.js';

test('link parsing (parseLink)', () => {
  assert.deepEqual(parseLink('?repo=a/b'), { repo: 'a/b', ref: undefined, path: '.kiotty/spec.kisp' }, 'path defaults to .kiotty/spec.kisp and ref is undefined when missing');
  assert.deepEqual(parseLink('?repo=a/b&ref=v1&path=x/y.kisp'), { repo: 'a/b', ref: 'v1', path: 'x/y.kisp' });
  assert.deepEqual(parseLink('?repo=a%2Fb&ref=%ED%83%9C%EA%B7%B8&path=%2Fdocs%2F%EC%82%AC%EC%96%91.kisp'), { repo: 'a/b', ref: '태그', path: 'docs/사양.kisp' }, 'encoded values are decoded');
  assert.equal(parseLink(''), null, 'no repo gives null');
  assert.equal(parseLink('?ref=v1'), null, 'no repo gives null');
  assert.equal(formatLink({ repo: 'a/b', ref: '', path: '.kiotty/spec.kisp' }), '?repo=a%2Fb');
  assert.equal(isRepoName('owner/name'), true);
  assert.equal(isRepoName('owner'), false);
  assert.equal(isRepoName('a/b/c'), false);
});

function fakeFetch(routes) {
  const calls = [];
  const fetchFn = async (url) => {
    calls.push(url);
    const route = routes.find(([pattern]) => url.includes(pattern));
    if (!route) throw new Error(`unexpected ${url}`);
    const answer = route[1];
    if (answer === 'reject') throw new TypeError('network');
    return {
      status: answer.status,
      ok: answer.status >= 200 && answer.status < 300,
      json: async () => answer.json ?? {},
      arrayBuffer: async () => new TextEncoder().encode(answer.text ?? '').buffer,
    };
  };
  return { fetchFn, calls };
}

async function code(promise) {
  try { await promise; return 'ok'; } catch (error) { return error.code; }
}

test('fetch and 404 cause (fetchSpec)', async () => {
  const link = { repo: 'o/n', ref: 'v1', path: '.kiotty/spec.kisp' };
  {
    const { fetchFn, calls } = fakeFetch([
      ['api.github.com/repos/o/n', { status: 200, json: { default_branch: 'trunk' } }],
      ['raw.githubusercontent.com/o/n/trunk/.kiotty/spec.kisp', { status: 200, text: 'abc' }],
    ]);
    const bytes = await createGithubSource(fetchFn).fetchSpec({ ...link, ref: undefined });
    assert.equal(new TextDecoder().decode(bytes), 'abc', '200 returns the bytes');
    assert.ok(calls[1].includes('/trunk/'), 'missing ref uses default_branch');
  }
  const raw404 = ['raw.githubusercontent.com', { status: 404 }];
  assert.equal(await code(createGithubSource(fakeFetch([raw404, ['commits', { status: 200 }], ['api.github.com/repos/o/n', { status: 404 }]]).fetchFn).fetchSpec(link)), 'REPO_NOT_FOUND');
  assert.equal(await code(createGithubSource(fakeFetch([raw404, ['commits', { status: 404 }], ['api.github.com/repos/o/n', { status: 200 }]]).fetchFn).fetchSpec(link)), 'REF_NOT_FOUND');
  assert.equal(await code(createGithubSource(fakeFetch([raw404, ['commits', { status: 200 }], ['api.github.com/repos/o/n', { status: 200 }]]).fetchFn).fetchSpec(link)), 'PATH_NOT_FOUND');
  assert.equal(await code(createGithubSource(fakeFetch([raw404, ['commits', { status: 200 }], ['api.github.com/repos/o/n', { status: 403 }]]).fetchFn).fetchSpec(link)), 'NOT_FOUND', 'API 403 gives NOT_FOUND');
  assert.equal(await code(createGithubSource(fakeFetch([['raw.githubusercontent.com', 'reject']]).fetchFn).fetchSpec(link)), 'NETWORK', 'rejected fetch gives NETWORK');
});
