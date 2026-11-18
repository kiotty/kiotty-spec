// FileWatcher (DSN-122, CR-019 D8): 열어 둔 로컬 파일이 바뀌면 알린다. 뷰어 나머지는 방식을 모른다.
// 핸들 + FileSystemObserver → observer, 핸들만 → 1 초 poll, File 스냅숏 → manual (지켜보지 않음).
// docs/viewer_observe_test.html 의 createFileWatcher 를 옮겼다.

export const POLL_MS = 1000;

// source: { handle?, file? }. onChange(): 내용이 바뀌었을 때 한 번. onError(error): 읽지 못함.
export function createFileWatcher(source, onChange, onError, options = {}) {
  const {
    FileSystemObserver = globalThis.FileSystemObserver,
    setInterval = globalThis.setInterval.bind(globalThis),
    clearInterval = globalThis.clearInterval.bind(globalThis),
    document = globalThis.document,
  } = options;
  const handle = source.handle;
  if (!handle) return { mode: 'manual', stop() {} };

  let last = null;
  let isBusy = false;
  let isStopped = false;
  let timer = null;
  let observer = null;
  let mode = 'poll';

  async function check() {
    if (isBusy || isStopped) return;
    isBusy = true;
    try {
      const file = await handle.getFile();
      const sig = `${file.lastModified}:${file.size}`;
      const isChanged = last !== null && sig !== last;
      last = sig;
      if (isChanged) onChange();
    } catch (error) {
      onError(error);
    } finally {
      isBusy = false;
    }
  }

  function startPolling() {
    mode = 'poll';
    if (timer === null) timer = setInterval(check, POLL_MS);
  }

  function stopPolling() {
    if (timer !== null) clearInterval(timer);
    timer = null;
  }

  function onVisibility() {
    if (mode !== 'poll' || isStopped) return;
    if (document.hidden) stopPolling();
    else { check(); startPolling(); }
  }

  check();
  if (FileSystemObserver) {
    mode = 'observer';
    observer = new FileSystemObserver((records) => {
      if (records.some((r) => r.type === 'errored')) {
        observer.disconnect();
        observer = null;
        startPolling();
      }
      check();
    });
    Promise.resolve(observer.observe(handle)).catch(() => {
      observer?.disconnect();
      observer = null;
      startPolling();
    });
  } else {
    startPolling();
  }
  document?.addEventListener?.('visibilitychange', onVisibility);

  return {
    get mode() { return mode; },
    stop() {
      isStopped = true;
      stopPolling();
      observer?.disconnect();
      document?.removeEventListener?.('visibilitychange', onVisibility);
    },
  };
}
