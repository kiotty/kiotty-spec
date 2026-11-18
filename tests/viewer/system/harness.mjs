// 뷰어 시스템 테스트 (ST-045 .. ST-051) 의 틀: ks 로 시험 프로젝트를 만들고, 화면 코드를 실제 viewer.wasm 과 가짜 DOM 으로 돌린다.
// 필요한 것: ks (환경 변수 KIOTTY_KS, 아니면 PATH) 와 wasm32 빌드의 viewer.js (KIOTTY_VIEWER_JS, 아니면 build-wasm · temp/build-wasm 아래).
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createWasmSpec } from '../../../app/viewer/web/src/infra/wasm_spec.js';
import { createGithubSource, parseLink } from '../../../app/viewer/web/src/datalayer/datasource/github_source.js';
import { createLocalSource } from '../../../app/viewer/web/src/datalayer/datasource/local_source.js';
import { createFileWatcher } from '../../../app/viewer/web/src/datalayer/datasource/file_watcher.js';
import { createSpecState } from '../../../app/viewer/web/src/domain/states/spec_state.js';
import { createApp } from '../../../app/viewer/web/src/presentation/view/app.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

export const viewerJs = [
  process.env.KIOTTY_VIEWER_JS,
  path.join(ROOT, 'build-wasm/app/viewer/wasm/viewer.js'),
  path.join(ROOT, 'temp/build-wasm/app/viewer/wasm/viewer.js'),
].filter(Boolean).find((p) => existsSync(p));

const KS = process.env.KIOTTY_KS || 'ks';

function hasKs() {
  try {
    execFileSync(KS, ['help'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

// node:test 의 skip 값: 빠진 것이 있으면 이유를 준다.
export const missing = !viewerJs ? 'viewer.js not built' : !hasKs() ? 'ks not found' : false;

// 앱 코드가 쓰는 브라우저 전역 (항목 표의 태그 거르기).
globalThis.CSS ??= { escape: (text) => String(text).replace(/["\\]/g, '\\$&') };

// ---- ks 시험 프로젝트 ----

export function createProject(name = 'demo') {
  const dir = mkdtempSync(path.join(tmpdir(), 'kiotty-viewer-st-'));
  const run = (...args) => execFileSync(KS, [...args, '--root', dir], { encoding: 'utf8' }).trim();
  const file = path.join(dir, '.kiotty', 'spec.kisp');
  run('init', '--name', name);
  return {
    dir,
    file,
    ks: run,
    // ks 의 TSV 출력을 머리글 이름의 객체 배열로.
    rows(...args) {
      const lines = run(...args).split(/\r?\n/).filter(Boolean);
      const header = lines[0].split('\t');
      return lines.slice(1).map((line) => Object.fromEntries(line.split('\t').map((v, k) => [header[k], v])));
    },
    add(kind, title, options = {}) {
      const args = ['item', 'add', kind, '--title', title];
      if (options.summary) args.push('--summary', options.summary);
      if (options.parent) args.push('--parent', options.parent);
      for (const tag of options.tags ?? []) args.push('--tag', tag);
      if (options.body) {
        const bodyFile = path.join(dir, 'body.md');
        writeFileSync(bodyFile, options.body);
        args.push('--body-file', bodyFile);
      }
      return run(...args);
    },
    relate(from, relation, to) { run('relation', 'add', from, relation, to); },
    start(step, description) { return run('checkpoint', 'start', String(step), '--description', description); },
    complete() { run('checkpoint', 'complete', '--actor', 'user'); },
    bytes() { return readFileSync(file); },
    hash() { return createHash('sha256').update(readFileSync(file)).digest('hex'); },
    remove() { rmSync(dir, { recursive: true, force: true }); },
  };
}

// ---- 가짜 브라우저 ----

// 가짜 fetch: routes 는 주소 → {status, body} . 부른 주소를 calls 에 남긴다. 없는 주소는 404.
export function createFetch(routes = {}) {
  const calls = [];
  async function fetch(url) {
    calls.push(url);
    const route = routes[url] ?? { status: 404, body: '' };
    const body = typeof route.body === 'string' ? new TextEncoder().encode(route.body) : route.body ?? new Uint8Array();
    return {
      ok: route.status >= 200 && route.status < 300,
      status: route.status,
      json: async () => JSON.parse(new TextDecoder().decode(body)),
      arrayBuffer: async () => new Uint8Array(body).buffer,
    };
  }
  return { fetch, calls };
}

export const RAW = 'https://raw.githubusercontent.com';
export const API = 'https://api.github.com';

// node:fs 로 읽는 FileSystemFileHandle.
export function fileHandle(file, { query = 'granted', request = 'granted' } = {}) {
  return {
    kind: 'file',
    name: path.basename(file),
    queryPermission: async () => query,
    requestPermission: async () => request,
    async getFile() {
      const bytes = readFileSync(file);
      const stat = statSync(file);
      return { name: path.basename(file), lastModified: stat.mtimeMs, size: bytes.length, arrayBuffer: async () => new Uint8Array(bytes).buffer };
    },
  };
}

// 고른 순간의 내용을 가진 File (사파리처럼 핸들이 없는 끌어 놓기).
export function fileSnapshot(file) {
  const bytes = readFileSync(file);
  return { name: path.basename(file), lastModified: 1, size: bytes.length, arrayBuffer: async () => new Uint8Array(bytes).buffer };
}

export function dropOf(handle, file) {
  const item = { kind: 'file', getAsFile: () => file ?? null };
  if (handle) item.getAsFileSystemHandle = async () => handle;
  return { items: [item] };
}

export function createHandleStore() {
  let value = null;
  return { load: async () => value, save: async (h) => { value = h; }, clear: async () => { value = null; } };
}

// 본문 (markdown) 을 받을 가짜 DOM 요소. 바뀐 HTML 로 다시 쓸 수 있다.
function element(tag) {
  return {
    tag, children: [], dataset: {}, attrs: {}, className: '',
    appendChild(child) { this.children.push(child); return child; },
    append(...nodes) { this.children.push(...nodes); },
    setAttribute(name, value) { this.attrs[name] = value; },
    set textContent(text) { this.children = [{ tag: '#text', text }]; },
    set href(value) { this.attrs.href = value; },
  };
}

const escapeText = (text) => String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

function serialize(node) {
  if (node.tag === '#text') return escapeText(node.text);
  if (node.html !== undefined) return node.html;
  if (node.tag === '#fragment') return node.children.map(serialize).join('');
  const attrs = { ...node.attrs };
  if (node.className) attrs.class = node.className;
  const open = [node.tag, ...Object.entries(attrs).map(([k, v]) => `${k}="${escapeText(v)}"`)].join(' ');
  return `<${open}>${node.children.map(serialize).join('')}</${node.tag}>`;
}

// 가짜 이벤트 대상: 고른 data- 속성과 태그로 closest 를 답한다.
export function target({ tag = 'div', data = {}, value } = {}) {
  const self = {
    tagName: tag.toUpperCase(), dataset: { ...data }, value,
    closest(selector) {
      const m = /^([a-z]*)(?:\[data-([a-z-]+)(?:="([^"]*)")?\])?$/.exec(selector);
      if (!m) return null;
      const [, wantTag, attr, wantValue] = m;
      if (wantTag && wantTag !== tag) return null;
      if (attr) {
        const key = attr.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
        if (self.dataset[key] === undefined) return null;
        if (wantValue !== undefined && self.dataset[key] !== wantValue) return null;
      }
      return self;
    },
  };
  return self;
}

// root: 앱이 innerHTML 로 그리는 자리. [data-md] 상자에 채운 본문은 html() 에서 제자리에 넣는다.
function createRoot() {
  const listeners = {};
  const boxes = [];
  let markup = '';
  return {
    get innerHTML() { return markup; },
    set innerHTML(value) { markup = value; boxes.length = 0; },
    addEventListener(type, fn) { (listeners[type] ??= []).push(fn); },
    querySelector() { return null; },
    querySelectorAll(selector) {
      if (selector !== '[data-md]') return [];
      for (const m of markup.matchAll(/data-md="([^"]*)"/g)) {
        const box = element('div');
        box.dataset.md = m[1].replace(/&amp;/g, '&');
        Object.defineProperty(box, 'innerHTML', { set(html) { this.children = [{ html }]; } });
        boxes.push(box);
      }
      return [...boxes];
    },
    dispatch(type, eventTarget) {
      const event = { type, target: eventTarget, preventDefault() {} };
      for (const fn of listeners[type] ?? []) fn(event);
    },
    html() {
      let k = 0;
      return markup.replace(/(data-md="[^"]*"[^>]*>)/g, (open) => {
        const box = boxes[k++];
        return box ? open + box.children.map(serialize).join('') : open;
      });
    },
  };
}

function createLocation(url) {
  const parsed = new URL(url, 'https://kiotty.github.io');
  return {
    pathname: parsed.pathname,
    search: parsed.search,
    hash: parsed.hash,
    assigned: [],
    assign(next) { this.assigned.push(next); },
  };
}

// 뷰어 하나 (브라우저 탭 하나). main.js 의 조립과 처음 열기를 같은 순서로 한다.
// options: url, fetch, handleStore, showOpenFilePicker
export async function openViewer({ url = '/', fetch = createFetch().fetch, handleStore = createHandleStore(), showOpenFilePicker } = {}) {
  const location = createLocation(url);
  const history = {
    replaceState(_, __, next) {
      const at = next.indexOf('#');
      location.hash = at >= 0 ? next.slice(at) : '';
      const query = next.indexOf('?');
      if (query >= 0) location.search = next.slice(query, at >= 0 ? at : undefined);
      else if (next.startsWith('/')) location.search = '';
    },
  };
  const document = {
    activeElement: null,
    createElement: element,
    createTextNode: (text) => ({ tag: '#text', text }),
    createDocumentFragment: () => element('#fragment'),
  };
  const root = createRoot();
  const watchers = [];
  const wasmSpec = createWasmSpec(async () => (await import(pathToFileURL(viewerJs).href)).default());
  const win = { document, showOpenFilePicker };
  const localSource = createLocalSource({ win, handleStore });
  const specState = createSpecState({
    wasmSpec,
    githubSource: createGithubSource(fetch),
    createWatcher: (source, onChange, onError) => {
      const watcher = createFileWatcher(source, onChange, onError, { FileSystemObserver: undefined, document: undefined });
      watchers.push(watcher);
      return watcher;
    },
  });
  const app = createApp({ root, specState, localSource, location, history, document });
  specState.subscribe(() => app.render());

  const link = parseLink(location.search);
  app.fillForm(link);
  await wasmSpec.load().catch((error) => specState.fail(error));
  if (link) {
    await localSource.forget();
    app.showProgressIfEmpty();
    await specState.openRepo(link);
    if (specState.get().phase === 'failed') history.replaceState(null, '', '#/open');
  } else {
    await app.restoreLocal();
  }
  app.render();

  return {
    app, root, location, specState,
    // # 를 바꾸고 그린다 (hashchange).
    go(hash) {
      location.hash = hash;
      app.render();
      return root.html();
    },
    html: () => root.html(),
    text: () => textOf(root.html()),
    click(data, tag) { root.dispatch('click', target({ tag, data })); },
    change(data, value) { root.dispatch('change', target({ tag: 'input', data, value })); },
    input(data, value) { root.dispatch('input', target({ tag: 'input', data, value })); },
    close() { for (const w of watchers) w.stop(); },
  };
}

// ---- 화면 HTML 읽기 ----

export function textOf(html) {
  return html.replace(/<[^>]+>/g, ' ').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&').replace(/\s+/g, ' ').trim();
}

// 항목 표의 줄: [{id, title, summary, tags: {이름: 글}}] (태그 열 이름은 머리글에서).
export function tableRows(html) {
  const tagNames = [...(/<thead><tr>(.*?)<\/tr><\/thead>/s.exec(html)?.[1] ?? '').matchAll(/<th>(.*?)<\/th>/g)].map((m) => textOf(m[1])).slice(3, -1);
  return [...html.matchAll(/<tr class="row[^"]*" data-toggle="([^"]*)">(.*?)<\/tr>/gs)].map(([, id, row]) => {
    const cell = (label) => textOf(new RegExp(`<td data-l="${label}"[^>]*>(.*?)</td>`, 's').exec(row)?.[1] ?? '');
    return { id, title: cell('제목'), summary: cell('요약'), tags: Object.fromEntries(tagNames.map((t) => [t, cell(t)])) };
  });
}

// 항목 상세의 관계 상자: 제목 → {관계 이름: [ID]}
export function relationSections(html) {
  const sections = {};
  for (const [, title, body] of html.matchAll(/<section class="card"><h2>([^<]*)<\/h2>(.*?)<\/section>/gs)) {
    const groups = {};
    for (const [, name, list] of body.matchAll(/<div class="relname">([^<]*?) <small>\d+<\/small><\/div><ul>(.*?)<\/ul>/gs)) {
      groups[name] = [...list.matchAll(/class="iid" href="#\/item\/([^"]+)"/g)].map((m) => decodeURIComponent(m[1]));
    }
    const plain = [...body.matchAll(/class="iid" href="#\/item\/([^"]+)"/g)].map((m) => decodeURIComponent(m[1]));
    sections[title] = Object.keys(groups).length ? groups : plain;
  }
  return sections;
}

// 조건이 맞을 때까지 기다린다 (실제 타이머).
export async function waitFor(check, ms = 2000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (await check()) return true;
    await new Promise((r) => setTimeout(r, 25));
  }
  return Boolean(await check());
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
