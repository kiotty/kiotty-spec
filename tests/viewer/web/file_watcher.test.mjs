// UT-114 observer mode, UT-115 poll and manual mode
import test from 'node:test';
import assert from 'node:assert/strict';
import { createFileWatcher } from '../../../app/viewer/web/src/datalayer/datasource/file_watcher.js';

const tick = () => new Promise((resolve) => setImmediate(resolve));

function fakeHandle() {
  const handle = {
    lastModified: 1, size: 10, fails: false, reads: 0, slow: null,
    async getFile() {
      handle.reads += 1;
      if (handle.slow) await handle.slow;
      if (handle.fails) throw new DOMException('gone', 'NotFoundError');
      return { lastModified: handle.lastModified, size: handle.size };
    },
  };
  return handle;
}

function fakeObserverClass(log) {
  return class {
    constructor(callback) { this.callback = callback; log.instance = this; }
    observe(handle) { log.observed = handle; return Promise.resolve(); }
    disconnect() { log.disconnected = true; }
  };
}

function fakeTimers() {
  const timers = { fns: new Map(), next: 1 };
  return {
    timers,
    setInterval: (fn) => { const id = timers.next++; timers.fns.set(id, fn); return id; },
    clearInterval: (id) => { timers.fns.delete(id); },
    async run() { for (const fn of [...timers.fns.values()]) fn(); await tick(); await tick(); },
  };
}

test('observer mode', async () => {
  const handle = fakeHandle();
  const log = {};
  const clock = fakeTimers();
  let changes = 0;
  const watcher = createFileWatcher({ handle }, () => { changes += 1; }, () => {}, {
    FileSystemObserver: fakeObserverClass(log), setInterval: clock.setInterval, clearInterval: clock.clearInterval, document: null,
  });
  await tick();
  assert.equal(watcher.mode, 'observer', 'mode is observer');
  assert.equal(log.observed, handle);
  log.instance.callback([{ type: 'modified' }]);
  await tick();
  assert.equal(changes, 0, 'records without change do not call onChange');
  handle.lastModified = 2;
  log.instance.callback([{ type: 'appeared' }]);
  log.instance.callback([{ type: 'modified' }]);
  log.instance.callback([{ type: 'moved' }]);
  await tick(); await tick();
  assert.equal(changes, 1, 'three records after one change call onChange once');
  log.instance.callback([{ type: 'errored' }]);
  await tick();
  assert.equal(watcher.mode, 'poll', 'errored record switches to poll');
  assert.equal(log.disconnected, true);
  assert.equal(clock.timers.fns.size, 1);
  watcher.stop();
  assert.equal(clock.timers.fns.size, 0, 'stop clears the timer');
});

test('poll and manual mode', async () => {
  const handle = fakeHandle();
  const clock = fakeTimers();
  const listeners = {};
  const doc = { hidden: false, addEventListener: (n, f) => { listeners[n] = f; }, removeEventListener: () => {} };
  let changes = 0;
  const errors = [];
  const watcher = createFileWatcher({ handle }, () => { changes += 1; }, (e) => errors.push(e), {
    FileSystemObserver: undefined, setInterval: clock.setInterval, clearInterval: clock.clearInterval, document: doc,
  });
  await tick();
  assert.equal(watcher.mode, 'poll');
  await clock.run();
  assert.equal(changes, 0);
  handle.size = 11;
  await clock.run();
  assert.equal(changes, 1, 'one tick after change calls onChange');

  let release;
  handle.slow = new Promise((resolve) => { release = resolve; });
  handle.size = 12;
  const before = handle.reads;
  for (const fn of clock.timers.fns.values()) { fn(); fn(); }
  assert.equal(handle.reads, before + 1, 'a tick while reading is skipped');
  release();
  handle.slow = null;
  await tick(); await tick();
  assert.equal(changes, 2);

  doc.hidden = true;
  listeners.visibilitychange();
  assert.equal(clock.timers.fns.size, 0, 'hidden tab stops ticks');
  handle.size = 13;
  doc.hidden = false;
  listeners.visibilitychange();
  await tick(); await tick();
  assert.equal(changes, 3, 'visible checks once');
  assert.equal(clock.timers.fns.size, 1, 'and ticks again');

  handle.fails = true;
  await clock.run();
  assert.equal(errors[0].name, 'NotFoundError', 'getFile failure calls onError');
  watcher.stop();

  const manual = createFileWatcher({ file: { name: 'a.kisp' } }, () => {}, () => {}, { setInterval: clock.setInterval, clearInterval: clock.clearInterval });
  assert.equal(manual.mode, 'manual', 'File source gives manual and never reads');
});
