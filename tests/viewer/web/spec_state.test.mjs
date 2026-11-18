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

test('state local open and reload', async () => {
  const opened = [];
  let failNext = false;
  const wasmSpec = {
    async open(bytes) {
      if (failNext) throw Object.assign(new Error('bad'), { code: 'FORMAT', line: 2 });
      opened.push(bytes[0]);
      return { name: `p${bytes[0]}`, created_at: 1 };
    },
    query: () => ({}),
  };
  const watchers = [];
  const createWatcher = (source, onChange, onError) => {
    const w = { mode: 'observer', stopped: false, onChange, onError, stop() { this.stopped = true; } };
    watchers.push(w);
    return w;
  };
  const source = (name, byte) => ({ name, read: async () => ({ bytes: new Uint8Array([byte]) }) });
  const state = createSpecState({ wasmSpec, githubSource: {}, createWatcher, now: () => 1000 });
  const seen = [];
  state.subscribe((s) => seen.push(s.revision));

  await state.openLocal(source('a.kisp', 1));
  assert.equal(state.get().phase, 'ready', 'open sets ready');
  assert.equal(state.get().revision, 1, 'open sets revision 1');
  assert.deepEqual(state.get().source, { type: 'local', name: 'a.kisp' });
  assert.equal(state.get().watch.mode, 'observer');

  failNext = true;
  watchers[0].onChange();
  await new Promise((r) => setImmediate(r));
  assert.equal(state.get().project.name, 'p1', 'failed reload keeps project');
  assert.equal(state.get().revision, 1, 'and revision');
  assert.equal(state.get().watch.error.code, 'FORMAT', 'and sets watch.error');

  failNext = false;
  await state.reload();
  assert.equal(state.get().watch.error, null, 'next success clears error');
  assert.equal(state.get().revision, 2, 'and increments revision');
  assert.equal(state.get().watch.reloadedAt, 1000);

  await state.openLocal(source('b.kisp', 2));
  assert.equal(watchers[0].stopped, true, 'opening another file stops the old watcher');
  assert.equal(state.get().source.name, 'b.kisp');

  failNext = true;
  await state.openLocal(source('c.kisp', 3));
  assert.equal(state.get().source.name, 'b.kisp', 'a bad new file keeps the open one');
  assert.equal(state.get().phase, 'ready');
  assert.equal(state.get().notice.name, 'c.kisp');
  assert.equal(watchers[1].stopped, false, 'and keeps watching it');
  assert.ok(seen.length >= 6, 'subscribers are told every change');
});
