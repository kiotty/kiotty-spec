// UT-118 state open and reload (V2: 저장소로 열기 부분)
import test from 'node:test';
import assert from 'node:assert/strict';
import { createSpecState } from '../../../app/viewer/web/src/domain/states/spec_state.js';

function fakes({ openFails = false, fetchFails = false } = {}) {
  return {
    githubSource: {
      async fetchSpec() {
        if (fetchFails) throw Object.assign(new Error('x'), { code: 'NETWORK' });
        return new Uint8Array([1]);
      },
    },
    wasmSpec: {
      async open() {
        if (openFails) throw Object.assign(new Error('bad'), { code: 'FORMAT', line: 3 });
        return { name: 'p', created_at: 1 };
      },
      query: (r) => ({ ok: true, request: r }),
    },
  };
}

test('state open (openRepo)', async () => {
  const state = createSpecState(fakes());
  const seen = [];
  state.subscribe((s) => seen.push(s.phase));
  const link = { repo: 'o/n', ref: 'v1', path: '.kiotty/spec.kisp' };
  await state.openRepo(link);
  assert.equal(state.get().phase, 'ready', 'open sets ready');
  assert.equal(state.get().revision, 1, 'open sets revision 1');
  assert.deepEqual(state.get().source, { type: 'repo', ...link });
  assert.deepEqual(seen, ['loading', 'ready'], 'subscribers are told every change');
  assert.equal(state.query('overview').request, 'overview');

  const failing = createSpecState(fakes({ openFails: true }));
  await failing.openRepo(link);
  assert.equal(failing.get().phase, 'failed');
  assert.equal(failing.get().openError.error.code, 'FORMAT');
  assert.deepEqual(failing.get().openError.link, link, 'failure keeps the link to refill the form');

  const offline = createSpecState(fakes({ fetchFails: true }));
  await offline.openRepo(link);
  assert.equal(offline.get().openError.error.code, 'NETWORK');
});
