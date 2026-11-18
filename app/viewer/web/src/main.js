// 조립 (DSN-116 의 di 역할): 브라우저 전역을 모듈에 넣고 처음 화면을 연다 (DSN-125).

import { createWasmSpec } from './infra/wasm_spec.js';
import { createGithubSource, parseLink } from './datalayer/datasource/github_source.js';
import { createSpecState } from './domain/states/spec_state.js';
import { createApp } from './presentation/view/app.js';

const wasmSpec = createWasmSpec(async () => {
  const { default: createKiottyViewer } = await import(new URL('../viewer.js', import.meta.url).href);
  return createKiottyViewer();
});
const specState = createSpecState({ wasmSpec, githubSource: createGithubSource() });
const app = createApp({ root: document.getElementById('app'), specState, location: window.location, history: window.history });

specState.subscribe(() => app.render());
window.addEventListener('hashchange', () => app.render());

const link = parseLink(window.location.search);
app.fillForm(link);
wasmSpec.load().catch((error) => specState.fail(error));
if (link) {
  app.showProgressIfEmpty();
  specState.openRepo(link).then(() => {
    if (specState.get().phase === 'failed') window.history.replaceState(null, '', '#/open');
    app.render();
  });
}
app.render();
