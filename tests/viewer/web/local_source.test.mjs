// UT-116 local source pick and drop, UT-117 handle restore
import test from 'node:test';
import assert from 'node:assert/strict';
import { createLocalSource } from '../../../app/viewer/web/src/datalayer/datasource/local_source.js';

function fakeStore(initial = null) {
  const store = { value: initial, saved: [], cleared: 0 };
  return {
    store,
    handleStore: {
      load: async () => store.value,
      save: async (h) => { store.value = h; store.saved.push(h); },
      clear: async () => { store.value = null; store.cleared += 1; },
    },
  };
}

const file = (name) => ({ name, lastModified: 1, size: 3, arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer });
const handle = (name, { query = 'granted', request = 'granted', fails = false } = {}) => ({
  kind: 'file', name,
  queryPermission: async () => query,
  requestPermission: async () => request,
  getFile: async () => { if (fails) throw new DOMException('gone', 'NotFoundError'); return file(name); },
});

function fakeDocument(picked) {
  return {
    createElement() {
      const listeners = {};
      return {
        files: picked ? [picked] : [],
        addEventListener(name, fn) { listeners[name] = fn; },
        click() { (picked ? listeners.change : listeners.cancel)(); },
      };
    },
  };
}

test('local source pick and drop', async () => {
  const { store, handleStore } = fakeStore();
  const h = handle('spec.kisp');
  const ok = await createLocalSource({ win: { showOpenFilePicker: async () => [h], document: fakeDocument(null) }, handleStore }).pick();
  assert.equal(ok.source.handle, h, 'picker success gives handle source');
  assert.equal(store.saved[0], h, 'handle is kept');
  assert.equal(ok.isBlocked, false);

  const abort = await createLocalSource({ win: { showOpenFilePicker: async () => { throw new DOMException('x', 'AbortError'); }, document: fakeDocument(file('x')) }, handleStore }).pick();
  assert.equal(abort, null, 'AbortError gives null');

  const blocked = await createLocalSource({ win: { showOpenFilePicker: async () => { throw new DOMException('x', 'SecurityError'); }, document: fakeDocument(file('spec.kisp')) }, handleStore }).pick();
  assert.equal(blocked.isBlocked, true, 'other rejection falls back to input with blocked');
  assert.equal(blocked.source.file.name, 'spec.kisp');
  assert.equal(blocked.source.handle, undefined);

  const noPicker = await createLocalSource({ win: { document: fakeDocument(file('a.kisp')) }, handleStore }).pick();
  assert.equal(noPicker.source.file.name, 'a.kisp', 'missing picker uses input');

  const drop = (items) => createLocalSource({ win: {}, handleStore }).fromDrop({ items });
  const two = await drop([
    { kind: 'file', getAsFileSystemHandle: async () => handle('one.kisp'), getAsFile: () => file('one.kisp') },
    { kind: 'file', getAsFileSystemHandle: async () => handle('two.kisp'), getAsFile: () => file('two.kisp') },
  ]);
  assert.equal(two.source.name, 'one.kisp', 'two files give first');
  assert.equal(two.isMany, true, 'and many');
  const plain = await drop([{ kind: 'file', getAsFile: () => file('p.kisp') }]);
  assert.equal(plain.source.file.name, 'p.kisp', 'drop without handle API gives File source');
  assert.equal(await drop([{ kind: 'string' }]), null, 'no file gives null');
  const read = await plain.source.read();
  assert.deepEqual([...read.bytes], [1, 2, 3]);
});

test('handle restore', async () => {
  {
    const h = handle('kept.kisp');
    const result = await createLocalSource({ win: {}, handleStore: fakeStore(h).handleStore }).restore();
    assert.equal(result.source.handle, h, 'granted opens the stored handle');
  }
  {
    const result = await createLocalSource({ win: {}, handleStore: fakeStore(handle('ask.kisp', { query: 'prompt' })).handleStore }).restore();
    assert.deepEqual(result, { needsPermission: true, name: 'ask.kisp' }, 'prompt gives needsPermission with name');
  }
  {
    const { store, handleStore } = fakeStore(handle('no.kisp', { query: 'prompt', request: 'denied' }));
    await assert.rejects(createLocalSource({ win: {}, handleStore }).reopen(), (e) => e.code === 'LOST');
    assert.equal(store.value, null, 'refused handle is removed from the store');
  }
  {
    const { store, handleStore } = fakeStore(handle('denied.kisp', { query: 'denied' }));
    assert.equal(await createLocalSource({ win: {}, handleStore }).restore(), null);
    assert.equal(store.value, null, 'failing handle is removed from the store');
  }
  {
    const { store, handleStore } = fakeStore(handle('x.kisp'));
    await createLocalSource({ win: {}, handleStore }).forget();
    assert.equal(store.value, null, 'opening a repo link clears the store');
  }
  assert.equal(await createLocalSource({ win: {}, handleStore: fakeStore(null).handleStore }).restore(), null);
});
