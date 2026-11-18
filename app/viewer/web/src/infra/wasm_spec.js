// WasmSpec (DSN-119): viewer.wasm 을 불러와 사양 파일 바이트를 열고 조회 JSON 을 객체로 돌려준다.
// JS 쪽에서 wasm 을 아는 유일한 모듈이다.

const SPEC_PATH = '/spec.kisp';
const TOO_LARGE = new Set(['CAPACITY_EXCEEDED', 'POOL_IS_FULL', 'OUT_OF_RANGE']);

export class SpecError extends Error {
  constructor(error) {
    super(`${error.status} ${error.subject} ${error.text}`.trim());
    this.name = 'SpecError';
    this.status = error.status;
    this.subject = error.subject;
    this.line = error.line;
    this.text = error.text;
    this.code = classify(error);
  }
}

export class UnsupportedError extends Error {
  constructor() {
    super('WebAssembly is not available');
    this.name = 'UnsupportedError';
    this.code = 'UNSUPPORTED';
  }
}

// 열기 실패를 UI-169 문구 종류로 나눈다.
function classify(error) {
  if (error.status === 'NOT_OPEN') return 'NOT_OPEN';
  if (error.subject === 'MODEL') return 'MODEL';
  if (TOO_LARGE.has(error.status) || error.subject === 'CONFIGURATION') return 'TOO_LARGE';
  if (error.status === 'NOT_FOUND') return 'NOT_FOUND';
  return 'FORMAT';
}

// createModule: () => Promise<emscripten module> (viewer.js 의 기본 내보내기)
export function createWasmSpec(createModule, { webAssembly = globalThis.WebAssembly } = {}) {
  let module = null;
  let loading = null;

  function call(name, text) {
    return JSON.parse(module.ccall(name, 'string', ['string'], [text]));
  }

  return {
    async load() {
      if (module) return;
      if (!webAssembly) throw new UnsupportedError();
      loading ??= createModule().then((m) => { module = m; });
      await loading;
    },
    async open(bytes) {
      await this.load();
      module.FS.writeFile(SPEC_PATH, bytes);
      const answer = call('KiottyViewer_open', SPEC_PATH);
      if (!answer.ok) throw new SpecError(answer.error);
      return answer.project;
    },
    query(request) {
      if (!module) throw new SpecError({ status: 'NOT_OPEN', subject: 'FILE', line: 0, text: '' });
      const answer = call('KiottyViewer_query', request);
      if (!answer.ok) throw new SpecError(answer.error);
      return answer;
    },
  };
}
