// SpecState (DSN-123): 원천 · 열기 상태 · revision 을 갖고 화면에 알린다.
// V2 는 저장소 링크로 열기만 한다. 로컬 파일 · 지켜보기 · 다시 읽기는 V4 (DSN-126).

export function createSpecState({ wasmSpec, githubSource }) {
  let state = {
    source: null,      // {type: 'repo', repo, ref, path}
    phase: 'idle',     // idle · loading · ready · failed
    project: null,
    revision: 0,
    openError: null,   // {error, link}
  };
  const listeners = new Set();

  function update(patch) {
    state = { ...state, ...patch };
    for (const listener of listeners) listener(state);
  }

  return {
    get() { return state; },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    query(request) { return wasmSpec.query(request); },
    async openRepo(link) {
      update({ phase: 'loading', openError: null });
      try {
        const bytes = await githubSource.fetchSpec(link);
        const project = await wasmSpec.open(bytes);
        update({
          source: { type: 'repo', ...link },
          phase: 'ready',
          project,
          revision: state.revision + 1,
        });
      } catch (error) {
        update({ phase: 'failed', openError: { error, link } });
      }
    },
    fail(error) {
      update({ phase: 'failed', openError: { error, link: null } });
    },
  };
}
