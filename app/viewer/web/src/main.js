// 조립 (DSN-116 의 di 역할): 브라우저 전역을 모듈에 넣고 처음 화면을 연다 (DSN-125, DSN-126).

import { createWasmSpec } from './infra/wasm_spec.js';
import { createGithubSource, parseLink } from './datalayer/datasource/github_source.js';
import { createHandleStore } from './datalayer/datasource/handle_store.js';
import { createLocalSource } from './datalayer/datasource/local_source.js';
import { createFileWatcher } from './datalayer/datasource/file_watcher.js';
import { createSpecState } from './domain/states/spec_state.js';
import { createApp } from './presentation/view/app.js';

const wasmSpec = createWasmSpec(async () => {
  const { default: createKiottyViewer } = await import(new URL('../viewer.js', import.meta.url).href);
  return createKiottyViewer();
});
const localSource = createLocalSource({ win: window, handleStore: createHandleStore() });
const specState = createSpecState({
  wasmSpec,
  githubSource: createGithubSource(),
  createWatcher: (source, onChange, onError) => createFileWatcher(source, onChange, onError),
});
const app = createApp({
  root: document.getElementById('app'), specState, localSource,
  location: window.location, history: window.history, document,
});

specState.subscribe(() => app.render());
window.addEventListener('hashchange', () => app.render());

// 뷰어 어디에든 끌어 놓으면 새 파일을 연다 (UI-170).
let dragDepth = 0;
const hasFiles = (event) => [...(event.dataTransfer?.types ?? [])].includes('Files');
window.addEventListener('dragenter', (event) => {
  if (!hasFiles(event)) return;
  dragDepth += 1;
  document.body.classList.add('dragging');
});
window.addEventListener('dragleave', () => {
  dragDepth = Math.max(0, dragDepth - 1);
  if (dragDepth === 0) document.body.classList.remove('dragging');
});
window.addEventListener('dragover', (event) => { if (hasFiles(event)) event.preventDefault(); });
window.addEventListener('drop', (event) => {
  if (!hasFiles(event)) return;
  event.preventDefault();
  dragDepth = 0;
  document.body.classList.remove('dragging');
  app.openDropped(event.dataTransfer);
});

const link = parseLink(window.location.search);
app.fillForm(link);
wasmSpec.load().catch((error) => specState.fail(error));
if (link) {
  localSource.forget();
  app.showProgressIfEmpty();
  specState.openRepo(link).then(() => {
    if (specState.get().phase === 'failed') window.history.replaceState(null, '', '#/open');
    app.render();
  });
} else {
  app.restoreLocal();
}
app.render();
