// local_source (DSN-121): 끌어 놓기 · 파일 고르기 · 보관한 핸들로 로컬 사양 파일을 받는다.
// 파일은 읽기만 하고 어디에도 보내지 않는다 (REQ-054).

export class LocalError extends Error {
  constructor(code, cause) {
    super(code);
    this.name = 'LocalError';
    this.code = code;
    this.cause = cause;
  }
}

function fromHandle(handle) {
  return {
    name: handle.name,
    handle,
    async read() {
      const file = await handle.getFile();
      return { bytes: new Uint8Array(await file.arrayBuffer()), lastModified: file.lastModified, size: file.size };
    },
  };
}

async function isReadable(handle) {
  try {
    await handle.getFile();
    return true;
  } catch {
    return false;
  }
}

function fromFile(file) {
  return {
    name: file.name,
    file,
    async read() {
      return { bytes: new Uint8Array(await file.arrayBuffer()), lastModified: file.lastModified, size: file.size };
    },
  };
}

// win: { showOpenFilePicker?, document } , handleStore: createHandleStore()
export function createLocalSource({ win = globalThis, handleStore }) {
  function pickWithInput() {
    return new Promise((resolve) => {
      const input = win.document.createElement('input');
      input.type = 'file';
      input.accept = '.kisp';
      input.addEventListener('change', () => resolve(input.files?.[0] ? fromFile(input.files[0]) : null));
      input.addEventListener('cancel', () => resolve(null));
      input.click();
    });
  }

  async function remember(source) {
    if (source?.handle) {
      try { await handleStore.save(source.handle); } catch { /* 보관하지 못해도 연다 */ }
    }
    return source;
  }

  return {
    // { source, isMany, isBlocked } 를 돌려준다. 파일이 없으면 null.
    // 브라우저가 막는 위치 (크롬의 AppData 등) 의 핸들은 읽지 못하므로 File 스냅숏으로 열고 isBlocked 를 알린다.
    async fromDrop(dataTransfer) {
      const items = [...(dataTransfer.items ?? [])].filter((i) => i.kind === 'file');
      if (items.length === 0) return null;
      const isMany = items.length > 1;
      const first = items[0];
      const file = first.getAsFile();
      if (first.getAsFileSystemHandle) {
        const handle = await first.getAsFileSystemHandle();
        const isFile = handle?.kind === 'file';
        if (isFile && await isReadable(handle)) return { source: await remember(fromHandle(handle)), isMany, isBlocked: false };
        if (isFile && file) return { source: fromFile(file), isMany, isBlocked: true };
      }
      return file ? { source: fromFile(file), isMany, isBlocked: false } : null;
    },

    // { source, isBlocked } 를 돌려준다. 고르지 않으면 null.
    async pick() {
      if (!win.showOpenFilePicker) {
        const source = await pickWithInput();
        return source ? { source, isBlocked: false } : null;
      }
      try {
        const [handle] = await win.showOpenFilePicker({ multiple: false });
        return { source: await remember(fromHandle(handle)), isBlocked: false };
      } catch (error) {
        if (error?.name === 'AbortError') return null;
        const source = await pickWithInput();
        return source ? { source, isBlocked: true } : null;
      }
    },

    // 보관한 핸들. 권한이 있으면 { source }, 물어야 하면 { needsPermission, name }, 없으면 null.
    async restore() {
      const handle = await handleStore.load().catch(() => null);
      if (!handle) return null;
      const permission = await handle.queryPermission?.({ mode: 'read' }) ?? 'granted';
      if (permission === 'granted') return { source: fromHandle(handle) };
      if (permission === 'prompt') return { needsPermission: true, name: handle.name };
      await handleStore.clear().catch(() => {});
      return null;
    },

    // "다시 열기" 단추: 사용자 클릭 안에서 권한을 묻는다.
    async reopen() {
      const handle = await handleStore.load().catch(() => null);
      if (!handle) throw new LocalError('LOST');
      const permission = await handle.requestPermission?.({ mode: 'read' }) ?? 'granted';
      if (permission !== 'granted') {
        await handleStore.clear().catch(() => {});
        throw new LocalError('LOST');
      }
      return fromHandle(handle);
    },

    forget: () => handleStore.clear().catch(() => {}),
  };
}
