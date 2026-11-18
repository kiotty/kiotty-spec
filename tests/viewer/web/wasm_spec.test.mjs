// UT-123 wasm module round trip. wasm32 빌드의 viewer.js 가 있어야 한다 (없으면 건너뛴다).
// 위치: 환경 변수 KIOTTY_VIEWER_JS, 아니면 build-wasm · temp/build-wasm 아래.
import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
import { createWasmSpec } from '../../../app/viewer/web/src/infra/wasm_spec.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const candidates = [
  process.env.KIOTTY_VIEWER_JS,
  path.join(root, 'build-wasm/app/viewer/wasm/viewer.js'),
  path.join(root, 'temp/build-wasm/app/viewer/wasm/viewer.js'),
].filter(Boolean);
const viewerJs = candidates.find((p) => existsSync(p));

test('wasm module round trip', { skip: viewerJs ? false : 'viewer.js not built' }, async () => {
  const spec = createWasmSpec(async () => (await import(pathToFileURL(viewerJs).href)).default());
  await spec.load();
  const good = readFileSync(path.join(root, '.kiotty/spec.kisp'));
  const project = await spec.open(good);
  assert.equal(project.name, 'kiotty-spec', 'open returns project');
  const before = spec.query('overview').kinds.find((k) => k.kind === 'requirement').count;
  await assert.rejects(spec.open(new TextEncoder().encode('hello\nworld\n')), (error) => {
    assert.equal(error.name, 'SpecError');
    assert.equal(error.code, 'FORMAT');
    assert.ok(error.line > 0, 'bad bytes throw SpecError with line');
    return true;
  });
  assert.equal(spec.query('overview').kinds.find((k) => k.kind === 'requirement').count, before, 'bad bytes keep previous');
  assert.ok(Array.isArray(spec.query('progress').checkpoints), 'query returns parsed object');
  assert.throws(() => spec.query('item:NOPE-1'), (error) => error.status === 'NOT_FOUND', 'unknown id throws SpecError NOT_FOUND');
  await assert.rejects(createWasmSpec(async () => ({}), { webAssembly: null }).load(), (error) => error.code === 'UNSUPPORTED');
});
