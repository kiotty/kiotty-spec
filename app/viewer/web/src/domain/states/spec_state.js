// SpecState (DSN-123): 원천 · 열기 상태 · revision · 지켜보기 상태를 갖고 화면에 알린다.
// 열기와 다시 읽기를 한 곳에서 다룬다: 형식이 다르면 앞 내용을 두고, 다시 읽어도 화면은 그대로다 (DSN-126).

export function createSpecState({ wasmSpec, githubSource, createWatcher, now = () => Date.now() }) {
  let state = {
    source: null,      // {type: 'repo', repo, ref, path} 또는 {type: 'local', name}
    phase: 'idle',     // idle · loading · ready · failed
    project: null,
    revision: 0,
    openError: null,   // {error, link}
    notice: null,      // 열린 뒤 알릴 것 (여러 파일 · 막힌 위치 · 열지 못한 새 파일)
    watch: null,       // {mode, reloadedAt, error}
  };
  const listeners = new Set();
  let watcher = null;
  let local = null;

  function update(patch) {
    state = { ...state, ...patch };
    for (const listener of listeners) listener(state);
  }

  function stopWatching() {
    watcher?.stop();
    watcher = null;
    local = null;
  }

  async function reload() {
    if (!local) return;
    const current = local;
    try {
      const { bytes } = await current.read();
      const project = await wasmSpec.open(bytes);
      if (current !== local) return;
      update({ project, revision: state.revision + 1, watch: { ...state.watch, reloadedAt: now(), error: null } });
    } catch (error) {
      if (current !== local) return;
      update({ watch: { ...state.watch, error } });
    }
  }

  return {
    get() { return state; },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    query(request) { return wasmSpec.query(request); },

    async openRepo(link) {
      update({ phase: 'loading', openError: null, notice: null });
      try {
        const bytes = await githubSource.fetchSpec(link);
        const project = await wasmSpec.open(bytes);
        stopWatching();
        update({ source: { type: 'repo', ...link }, phase: 'ready', project, revision: state.revision + 1, watch: null });
      } catch (error) {
        update({ phase: 'failed', openError: { error, link } });
      }
    },

    // source: local_source 의 LocalSource. notice: 열린 뒤 보일 알림 (여러 파일 · 막힌 위치).
    async openLocal(source, notice = null) {
      const hadProject = Boolean(state.project);
      update({ phase: 'loading', notice: null });
      try {
        const { bytes } = await source.read();
        const project = await wasmSpec.open(bytes);
        stopWatching();
        local = source;
        watcher = createWatcher(source, () => reload(), (error) => {
          update({ watch: { ...state.watch, error } });
        });
        update({
          source: { type: 'local', name: source.name },
          phase: 'ready',
          project,
          revision: state.revision + 1,
          openError: null,
          notice,
          watch: { mode: watcher.mode, reloadedAt: null, error: null },
        });
      } catch (error) {
        // 앞서 연 파일이 있으면 그대로 두고 알리기만 한다 (UI-170).
        if (hadProject) update({ phase: 'ready', notice: { error, name: source.name } });
        else update({ phase: 'failed', openError: { error, link: null } });
      }
    },

    reload,
    fail(error) {
      update({ phase: 'failed', openError: { error, link: null } });
    },
    notify(notice) {
      update({ notice });
    },
  };
}
