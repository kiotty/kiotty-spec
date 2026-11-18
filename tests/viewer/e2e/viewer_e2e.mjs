// E2E-021 브라우저 뷰어: 크롬 · 엣지를 headless 로 띄워 배포본과 로컬본을 끝까지 쓴다.
// node tests/viewer/e2e/viewer_e2e.mjs [chrome|edge|all] [--manual]   (기본 all. --manual 은 7 의 권한 창을 사람이 누른다)
// 필요한 것: ks (PATH 또는 KIOTTY_KS), wasm32 빌드의 viewer.js · viewer.wasm (KIOTTY_VIEWER_JS 또는 build-wasm · temp/build-wasm),
// 인터넷 (배포본 · GitHub). 마지막 줄은 "N failed".
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { findBrowser, launch, serve } from './browser.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const KS = process.env.KIOTTY_KS || 'ks';
// KIOTTY_PAGES_URL=local 이면 배포본 대신 로컬본으로 1 · 2 · 9 를 돈다 (배포 전 확인).
const PAGES = process.env.KIOTTY_PAGES_URL || 'https://kiotty.github.io/kiotty-spec/';
const REPO = 'kiotty/kiotty-spec';
const TAG = process.env.KIOTTY_E2E_TAG || 'v0.3.0';
// 크롬은 AppData (임시 폴더 포함) 아래 파일의 핸들 읽기를 막으므로 저장소 temp/ 아래에서 시험한다.
const WORK = path.join(ROOT, 'temp', 'viewer-e2e');
const SHOTS = path.join(WORK, 'shots');
const viewerJs = [
  process.env.KIOTTY_VIEWER_JS,
  path.join(ROOT, 'build-wasm/app/viewer/wasm/viewer.js'),
  path.join(ROOT, 'temp/build-wasm/app/viewer/wasm/viewer.js'),
].filter(Boolean).find((p) => existsSync(p));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ks = (root, ...args) => execFileSync(KS, [...args, '--root', root], { encoding: 'utf8' }).replace(/\r/g, '').trim();
const rows = (root, ...args) => ks(root, ...args).split('\n').slice(1).filter(Boolean);

let failed = 0;
function check(label, condition, detail = '') {
  if (!condition) failed += 1;
  console.log(`${condition ? 'PASS' : 'FAIL'} ${label}${!condition && detail ? ` — ${detail}` : ''}`);
  return condition;
}

// 시험 프로젝트: 요구사항 둘 (하나는 위험한 본문) · 설계 · 변경 요청, 체크포인트 둘.
function createProject() {
  mkdirSync(WORK, { recursive: true });
  const dir = mkdtempSync(path.join(WORK, 'project-'));
  ks(dir, 'init', '--name', 'e2e');
  ks(dir, 'checkpoint', 'start', '1', '--description', '변경 요청 받기');
  ks(dir, 'item', 'add', 'requirement', '--title', '로그인한다', '--body', '## 인수 조건\n- 된다\n');
  const body = path.join(dir, 'body.md');
  writeFileSync(body, [
    '## 진술', '- 하나', '- 둘', '', '| 이름 | 값 |', '| --- | --- |', '| a | b |', '', '```', 'code <b>x</b>', '```',
    '<script>window.__e2e_xss = 1</script>', '<img src=x onerror="window.__e2e_xss = 2">', '',
  ].join('\n'));
  ks(dir, 'item', 'add', 'requirement', '--title', '위험한 본문', '--body-file', body);
  ks(dir, 'item', 'add', 'design', '--title', '로그인 모듈');
  ks(dir, 'relation', 'add', 'DSN-001', '관련 요구사항', 'REQ-001');
  ks(dir, 'checkpoint', 'complete', '--actor', 'user');
  ks(dir, 'checkpoint', 'start', '2', '--description', '요구사항 쓰기');
  return { dir, file: path.join(dir, '.kiotty', 'spec.kisp') };
}

// 태그의 사양 파일을 임시 루트에 꺼내 ks 로 센다.
function countsAtTag() {
  const dir = mkdtempSync(path.join(WORK, 'tag-'));
  mkdirSync(path.join(dir, '.kiotty'));
  writeFileSync(path.join(dir, '.kiotty', 'spec.kisp'), execFileSync('git', ['-C', ROOT, 'show', `${TAG}:.kiotty/spec.kisp`], { maxBuffer: 1 << 30 }));
  const kinds = rows(dir, 'model').map((line) => line.split('\t')[0]);
  const counts = Object.fromEntries(kinds.map((kind) => [kind, rows(dir, 'item', 'list', '--kind', kind).length]));
  rmSync(dir, { recursive: true, force: true });
  return counts;
}

function watchProblems(page) {
  const problems = [];
  page.on('Runtime.exceptionThrown', (e) => problems.push(`exception: ${e.exceptionDetails.exception?.description ?? e.exceptionDetails.text}`));
  page.on('Runtime.consoleAPICalled', (e) => { if (e.type === 'error') problems.push(`console: ${e.args.map((a) => a.value).join(' ')}`); });
  page.on('Log.entryAdded', (e) => { if (e.entry.level === 'error') problems.push(`log: ${e.entry.source} ${e.entry.text} ${e.entry.url ?? ''}`); });
  return problems;
}

const text = (page) => page.evaluate('document.body.innerText');
const hash = (page) => page.evaluate('location.hash');
const click = (page, selector) => page.evaluate(`(() => { const el = document.querySelector(${JSON.stringify(selector)}); if (!el) return false; el.click(); return true; })()`, { gesture: true });

async function goBack(page) {
  const history = await page.send('Page.getNavigationHistory');
  await page.send('Page.navigateToHistoryEntry', { entryId: history.entries[history.currentIndex - 1].id });
  await sleep(300);
}

async function dropFile(page, file) {
  const data = { items: [], files: [file], dragOperationsMask: 1 };
  for (const type of ['dragEnter', 'dragOver', 'drop']) await page.send('Input.dispatchDragEvent', { type, x: 300, y: 400, data });
}

// 1 · 2 · 9: 배포본
async function deployed(page, name, counts, origin) {
  const problems = watchProblems(page);
  const site = PAGES === 'local' ? `${origin}/` : PAGES;
  await page.goto(`${site}?repo=${REPO}&ref=${TAG}`);
  const opened = await page.waitFor('!!document.querySelector(".card.current")', 30000);
  check(`${name} 1 deployed site opens ${REPO}@${TAG} on the progress screen`, opened && (await hash(page)) === '#/progress');
  const source = await page.evaluate('document.querySelector(".src")?.innerText ?? ""');
  check(`${name} 1 header shows the repo and tag`, source.includes(`${REPO} @ ${TAG}`), source);
  await click(page, 'nav.tabs a[href="#/overview"]');
  await page.waitFor('!!document.querySelector(".counts")');
  const shown = await page.evaluate('Object.fromEntries([...document.querySelectorAll(".counts a")].map((a) => [decodeURIComponent(a.getAttribute("href").split("/").pop()), Number(a.querySelector("b").textContent)]))');
  const wrong = Object.keys(counts).filter((k) => shown[k] !== counts[k]);
  check(`${name} 1 overview counts equal ks item list at ${TAG}`, wrong.length === 0, wrong.map((k) => `${k} ${shown[k]} != ${counts[k]}`).join(', '));

  const visited = [];
  for (const tab of ['#/overview', '#/progress', '#/items/requirement', '#/changes']) {
    await click(page, `nav.tabs a[href="${tab}"]`);
    await sleep(200);
    visited.push((await hash(page)) === tab && (await page.evaluate(`document.querySelector('nav.tabs a.on')?.getAttribute('href')`)) === tab);
  }
  check(`${name} 2 tabs change the screen and the address`, visited.every(Boolean), JSON.stringify(visited));
  await click(page, 'nav.tabs a[href="#/items/requirement"]');
  await page.waitFor(`!!document.querySelector('a.iid[href="#/item/REQ-052"]')`);
  await click(page, 'a.iid[href="#/item/REQ-052"]');
  await page.waitFor('location.hash === "#/item/REQ-052"');
  const target = await page.evaluate('document.querySelector(".relgroup a.iid")?.getAttribute("href")');
  await click(page, '.relgroup a.iid');
  const moved = await page.waitFor(`location.hash === ${JSON.stringify(target)} && document.querySelector(".iid.big")?.textContent === ${JSON.stringify(decodeURIComponent((target ?? '').split('/').pop()))}`);
  await goBack(page);
  const back1 = await page.waitFor('location.hash === "#/item/REQ-052" && document.querySelector(".iid.big")?.textContent === "REQ-052"');
  await goBack(page);
  const back2 = await page.waitFor('location.hash === "#/items/requirement" && !!document.querySelector("table.items")');
  check(`${name} 2 relation link then back twice returns to REQ-052 and the table`, Boolean(moved && back1 && back2), `${target} ${moved} ${back1} ${back2}`);

  await page.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  const narrow = [];
  for (const screen of ['#/progress', '#/items/requirement', '#/item/REQ-052']) {
    await page.evaluate(`location.hash = ${JSON.stringify(screen)}`);
    await sleep(400);
    const width = await page.evaluate('document.documentElement.scrollWidth');
    const labels = await page.evaluate('[...document.querySelectorAll("td[data-l]")].slice(0, 3).map((td) => getComputedStyle(td, "::before").content)');
    const shot = await page.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(path.join(SHOTS, `${name}${screen.replace(/[#/]/g, '_')}.png`), Buffer.from(shot.data, 'base64'));
    narrow.push({ screen, width, labels });
  }
  await page.send('Emulation.clearDeviceMetricsOverride');
  check(`${name} 9 390 px screens have no sideways scroll`, narrow.every((n) => n.width <= 390), JSON.stringify(narrow.map((n) => [n.screen, n.width])));
  check(`${name} 9 table cells show their column names`, narrow.filter((n) => n.labels.length).every((n) => n.labels.every((c) => c && c !== 'none' && c !== 'normal')), JSON.stringify(narrow.map((n) => n.labels)));
  check(`${name} 1 no console error or CSP violation`, problems.length === 0, problems.join(' | '));
}

// 3 · 4 · 5 · 6 · 7 · 8: 로컬본
async function local(page, name, origin) {
  const problems = watchProblems(page);
  for (const [query, expected] of [
    ['?repo=kiotty/no-such-repo-e2e', '저장소 kiotty/no-such-repo-e2e 을 찾지 못했습니다'],
    [`?repo=${REPO}&ref=no-such-ref-e2e`, '버전 no-such-ref-e2e 를 찾지 못했습니다'],
  ]) {
    await page.goto(`${origin}/${query}`);
    const shown = await page.waitFor(`document.querySelector(".alert")?.innerText.includes(${JSON.stringify(expected)}) && document.querySelector(".alert").innerText.includes("로컬 파일로 여세요")`, 20000);
    check(`${name} 3 ${query} tells the cause and the local file hint`, Boolean(shown), await page.evaluate('document.querySelector(".alert")?.innerText ?? "(no alert)"'));
  }

  const project = createProject();
  const requests = [];
  page.on('Network.requestWillBeSent', (e) => requests.push(e.request.url));
  const choosers = [];
  page.on('Page.fileChooserOpened', (e) => choosers.push(e.mode));
  await page.send('Page.setInterceptFileChooserDialog', { enabled: true });
  await page.goto(`${origin}/`);
  await page.waitFor('!!document.querySelector("[data-act=pick]")');
  await click(page, '[data-act="pick"]');
  check(`${name} 4 pick file opens the file chooser`, await (async () => { await sleep(500); return choosers.includes('selectSingle'); })(), JSON.stringify(choosers));
  await page.send('Page.setInterceptFileChooserDialog', { enabled: false });
  await page.goto(`${origin}/`);
  await page.waitFor('!!document.querySelector("[data-act=pick]")');
  requests.length = 0;
  await dropFile(page, project.file);
  const opened = await page.waitFor('document.querySelector(".src")?.innerText.includes("spec.kisp") && !!document.querySelector(".card.current")', 10000);
  const top = await page.evaluate('document.querySelector(".topbar")?.innerText ?? ""');
  check(`${name} 4 a dropped file opens with its name and auto reload`, Boolean(opened) && top.includes('자동 갱신') && !top.includes('자동 갱신 안 됨'), top);
  const outside = requests.filter((url) => !url.startsWith(origin) && !url.startsWith('data:') && !url.startsWith('blob:'));
  check(`${name} 4 nothing leaves 127.0.0.1 while opening`, outside.length === 0, outside.join(' '));

  await page.evaluate('location.hash = "#/item/REQ-001"');
  await sleep(1200);
  const revisionText = () => page.evaluate('document.querySelector(".topbar")?.innerText ?? ""');
  const started = Date.now();
  ks(project.dir, 'item', 'add', 'design', '--title', 'E2E 새 설계');
  const reloaded = await page.waitFor('document.querySelector(".topbar")?.innerText.includes("다시 읽음")', 2000);
  const took = Date.now() - started;
  check(`${name} 5 a ks write is reloaded within 2 seconds and the address stays`, Boolean(reloaded) && (await hash(page)) === '#/item/REQ-001', `${took} ms, ${await hash(page)}`);
  await page.evaluate('location.hash = "#/items/design"');
  check(`${name} 5 the new design is in the table`, Boolean(await page.waitFor('document.body.innerText.includes("E2E 새 설계")', 2000)));

  const good = readFileSync(project.file);
  await sleep(1100);
  writeFileSync(project.file, 'broken\n');
  const broke = await page.waitFor('document.querySelector(".topbar")?.innerText.includes("다시 읽지 못했습니다")', 3000);
  const kept = (await text(page)).includes('E2E 새 설계');
  await sleep(1100);
  writeFileSync(project.file, good);
  ks(project.dir, 'item', 'add', 'design', '--title', 'E2E 고친 뒤 설계');
  const healed = await page.waitFor('!document.querySelector(".topbar")?.innerText.includes("다시 읽지 못했습니다") && document.body.innerText.includes("E2E 고친 뒤 설계")', 3000);
  check(`${name} 6 a broken write keeps the old content and recovers`, Boolean(broke && kept && healed), `${broke} ${kept} ${healed} ${await revisionText()}`);

  await page.send('Page.reload');
  await sleep(1500);
  await page.waitFor('document.readyState === "complete"');
  const restored = await page.waitFor('document.querySelector(".src")?.innerText.includes("spec.kisp") || !!document.querySelector("[data-act=reopen]")', 5000);
  const reopen = await page.evaluate('document.querySelector("[data-act=reopen]")?.innerText ?? ""');
  let result = 'not restored';
  if (await page.evaluate('!!document.querySelector(".src")?.innerText.includes("spec.kisp")')) result = 'opened at once';
  else if (reopen) {
    await click(page, '[data-act="reopen"]');
    result = (await page.waitFor('!!document.querySelector(".src")?.innerText.includes("spec.kisp")', 5000)) ? 'opened after the button' : `button shown (${reopen}), permission prompt needs a person`;
  }
  check(`${name} 7 refresh reopens the kept file (${result})`, Boolean(restored) && (result.startsWith('opened') || reopen.includes('spec.kisp')), result);

  await page.goto(`${origin}/`);
  await page.waitFor('!!document.querySelector("[data-act=pick]") || !!document.querySelector(".src")');
  await dropFile(page, project.file);
  await page.waitFor('document.querySelector(".src")?.innerText.includes("spec.kisp")', 10000);
  await page.evaluate('location.hash = "#/item/REQ-002"');
  await page.waitFor('!!document.querySelector(".md table")');
  const body = await page.evaluate('(() => { const md = document.querySelector("section.md"); return { table: !!md.querySelector("table"), list: !!md.querySelector("ul li"), code: !!md.querySelector("pre"), script: !!md.querySelector("script"), img: !!md.querySelector("img"), xss: window.__e2e_xss ?? null, text: md.innerText.includes("<script>") }; })()');
  await sleep(500);
  const xss = await page.evaluate('window.__e2e_xss ?? null');
  check(`${name} 8 a body draws table, list and code and never runs HTML`, body.table && body.list && body.code && !body.script && !body.img && xss === null && body.text, JSON.stringify({ ...body, xss }));
  check(`${name} 3-8 no uncaught exception`, problems.filter((p) => p.startsWith('exception')).length === 0, problems.join(' | '));
  return project;
}

async function run(name) {
  const executable = findBrowser(name);
  if (!executable) {
    console.log(`SKIP ${name}: not installed`);
    return;
  }
  const site = path.join(ROOT, 'app/viewer/web');
  const server = await serve(site, { '/viewer.js': viewerJs, '/viewer.wasm': viewerJs.replace(/\.js$/, '.wasm') });
  const browser = await launch(executable);
  console.log(`# ${name}: ${browser.version}`);
  const { page } = browser;
  await page.send('Page.enable');
  await page.send('Runtime.enable');
  await page.send('Log.enable');
  await page.send('Network.enable');
  let project = null;
  try {
    await deployed(page, name, countsAtTag(), server.origin);
    project = await local(page, name, server.origin);
  } catch (error) {
    check(`${name} run`, false, error.stack);
  } finally {
    await browser.close();
    await server.close();
    if (project) rmSync(project.dir, { recursive: true, force: true });
  }
}

// 7 수동: 창을 띄워 끌어 놓고 새로 고친 뒤 "다시 열기" 를 누른다. 권한 창의 허용은 사람이 누른다.
async function manual(name) {
  const site = path.join(ROOT, 'app/viewer/web');
  const server = await serve(site, { '/viewer.js': viewerJs, '/viewer.wasm': viewerJs.replace(/\.js$/, '.wasm') });
  const browser = await launch(findBrowser(name), { headless: false });
  const { page } = browser;
  const project = createProject();
  try {
    await page.send('Page.enable');
    await page.send('Runtime.enable');
    await page.goto(`${server.origin}/`);
    await page.waitFor('!!document.querySelector("[data-act=pick]")');
    await dropFile(page, project.file);
    await page.waitFor('document.querySelector(".src")?.innerText.includes("spec.kisp")', 10000);
    await page.send('Page.reload');
    await sleep(1500);
    const shown = await page.waitFor('!!document.querySelector("[data-act=reopen]") || !!document.querySelector(".src")?.innerText.includes("spec.kisp")', 10000);
    if (await page.evaluate('!!document.querySelector("[data-act=reopen]")')) {
      await click(page, '[data-act="reopen"]');
      console.log(`>>> ${name} 창의 권한 창에서 "허용" 을 눌러 주세요 (90 초 기다림)`);
    }
    const opened = await page.waitFor('!!document.querySelector(".src")?.innerText.includes("spec.kisp") && !!document.querySelector(".card.current")', 90000);
    check(`${name} 7 manual: refresh, reopen button and the permission prompt reopen the kept file`, Boolean(shown && opened));
  } finally {
    await browser.close();
    await server.close();
    rmSync(project.dir, { recursive: true, force: true });
  }
}

if (!viewerJs) {
  console.log('viewer.js not built');
  process.exit(1);
}
mkdirSync(SHOTS, { recursive: true });
const isManual = process.argv.includes('--manual');
const which = process.argv.slice(2).find((a) => !a.startsWith('--')) ?? 'all';
for (const name of which === 'all' ? ['chrome', 'edge'] : [which]) {
  if (isManual) await manual(name);
  else await run(name);
}
console.log(`${failed} failed`);
process.exit(failed ? 1 : 0);
