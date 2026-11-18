// handle_store (DSN-121): 로컬 파일 핸들(위치만, 내용 아님) 하나를 IndexedDB 에 보관한다.

const DB_NAME = 'kiotty-viewer';
const STORE = 'handle';
const KEY = 'local';

export function createHandleStore(indexedDB = globalThis.indexedDB) {
  function openDb() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DB_NAME, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(STORE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }

  async function run(mode, action) {
    if (!indexedDB) return undefined;
    const db = await openDb();
    return new Promise((resolve, reject) => {
      const tx = db.transaction(STORE, mode);
      const request = action(tx.objectStore(STORE));
      tx.oncomplete = () => resolve(request?.result);
      tx.onerror = () => reject(tx.error);
    });
  }

  return {
    load: () => run('readonly', (store) => store.get(KEY)),
    save: (handle) => run('readwrite', (store) => store.put(handle, KEY)),
    clear: () => run('readwrite', (store) => store.delete(KEY)),
  };
}
