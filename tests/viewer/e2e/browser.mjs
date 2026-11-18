// E2E 용 브라우저 구동 (E2E-021): 크롬 · 엣지를 headless 로 띄우고 DevTools 프로토콜로 다룬다. npm 없이 Node 내장 WebSocket 을 쓴다.
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import path from 'node:path';

export const BROWSERS = {
  chrome: [
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/usr/bin/google-chrome',
  ],
  edge: [
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
    '/usr/bin/microsoft-edge',
  ],
};

export function findBrowser(name) {
  return BROWSERS[name].find((p) => existsSync(p)) ?? null;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function readPort(profile) {
  const file = path.join(profile, 'DevToolsActivePort');
  for (let k = 0; k < 200; k += 1) {
    try {
      const [port] = readFileSync(file, 'utf8').split('\n');
      if (port) return Number(port);
    } catch { /* 아직 없거나 브라우저가 쓰는 중 */ }
    await sleep(50);
  }
  throw new Error('browser did not open a DevTools port');
}

// 페이지 하나에 붙은 DevTools 세션.
class Page {
  constructor(socket) {
    this.socket = socket;
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Map();
    socket.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (message.id && this.pending.has(message.id)) {
        const { resolve, reject } = this.pending.get(message.id);
        this.pending.delete(message.id);
        if (message.error) reject(new Error(`${message.error.message} ${message.error.data ?? ''}`));
        else resolve(message.result);
      } else if (message.method) {
        for (const fn of this.listeners.get(message.method) ?? []) fn(message.params);
      }
    });
  }

  send(method, params = {}) {
    const id = this.nextId++;
    this.socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  on(method, fn) {
    if (!this.listeners.has(method)) this.listeners.set(method, []);
    this.listeners.get(method).push(fn);
  }

  // 페이지 안에서 식을 계산해 값을 돌려준다. gesture 는 사용자 클릭처럼 다룬다 (파일 고르기 창이 필요로 한다).
  async evaluate(expression, { gesture = false } = {}) {
    const result = await this.send('Runtime.evaluate', {
      expression, awaitPromise: true, returnByValue: true, userGesture: gesture,
    });
    if (result.exceptionDetails) throw new Error(`evaluate: ${result.exceptionDetails.exception?.description ?? result.exceptionDetails.text}`);
    return result.result.value;
  }

  async waitFor(expression, ms = 10000) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      try {
        const value = await this.evaluate(expression);
        if (value) return value;
      } catch { /* 페이지가 바뀌는 중 */ }
      await sleep(50);
    }
    return false;
  }

  async goto(url) {
    await this.send('Page.navigate', { url });
    await this.waitFor('document.readyState === "complete"');
  }
}

// 브라우저를 띄우고 빈 페이지 하나에 붙는다. close() 로 닫고 프로필을 지운다.
export async function launch(executable, { width = 1280, height = 900, headless = true } = {}) {
  const profile = mkdtempSync(path.join(tmpdir(), 'kiotty-e2e-profile-'));
  const child = spawn(executable, [
    ...(headless ? ['--headless=new'] : []), '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', `--window-size=${width},${height}`, 'about:blank',
  ], { stdio: 'ignore' });
  const port = await readPort(profile);
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find((t) => t.type === 'page');
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true });
    socket.addEventListener('error', reject, { once: true });
  });
  const page = new Page(socket);
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  return {
    page,
    version: version.Browser,
    async close() {
      socket.close();
      child.kill();
      await sleep(500);
      rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    },
  };
}

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm',
  '.svg': 'image/svg+xml',
};

// files: 주소 경로 → 디스크 파일. 나머지는 root 아래에서 찾는다.
export function serve(root, files = {}) {
  const server = createServer((request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1');
    const name = url.pathname === '/' ? '/index.html' : decodeURIComponent(url.pathname);
    const file = files[name] ?? path.join(root, name);
    const isInside = files[name] || path.resolve(file).startsWith(path.resolve(root));
    if (!isInside || !existsSync(file)) {
      response.writeHead(404);
      response.end();
      return;
    }
    response.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    response.end(readFileSync(file));
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => resolve({
      origin: `http://127.0.0.1:${server.address().port}`,
      close: () => new Promise((r) => server.close(r)),
    }));
  });
}
