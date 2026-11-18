// 뷰어 시스템 테스트 ST-045 .. ST-051 (CR-019). 시험마다 ks 로 프로젝트를 만들고, 뷰어 화면을 같은 파일의 ks 출력과 비교한다.
// "E2E" 인 경우 (실제 브라우저 · 권한 창 · 네트워크 기록) 는 Step 11 의 E2E 테스트가 확인한다.
import test from 'node:test';
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import {
  API, RAW, createFetch, createHandleStore, createProject, dropOf, fileHandle, fileSnapshot, missing, openViewer,
  relationSections, sleep, tableRows, textOf, waitFor,
} from './harness.mjs';
import { STATE_NAMES, STEPS } from '../../../app/viewer/web/src/i18n/ko.js';

const skip = missing;
const KIND_TAG = '요구사항 종류';

// 요구사항 넷 (하나는 하위) · 설계 · 시스템 테스트 계획 · 변경 요청, 체크포인트 둘.
function richProject() {
  const p = createProject('demo');
  p.start(1, '변경 요청 받기');
  p.add('requirement', 'Login 화면', { summary: '사용자가 로그인한다', tags: [`${KIND_TAG}=기능 요구사항`], body: '## 인수 조건\n- 로그인된다\n' });
  p.add('requirement', '윈도우에서 돈다', { tags: [`${KIND_TAG}=환경 요구사항`], body: '## 인수 조건\n- 돈다\n' });
  p.add('requirement', '1 초 안에 연다', { tags: [`${KIND_TAG}=품질 요구사항`], body: '## 인수 조건\n- 빠르다\n' });
  p.add('requirement', '맥에서 돈다', { parent: 'REQ-001', tags: [`${KIND_TAG}=환경 요구사항`], body: '## 인수 조건\n- 돈다\n' });
  p.add('design', '로그인 모듈', { tags: ['설계 종류=모듈'] });
  p.add('system_test_plan', '로그인 시험');
  p.add('change_request', '로그인을 더한다', { tags: ['상태=열림', '진입 단계=Step 2'], body: '로그인을 더한다' });
  p.relate('DSN-001', '관련 요구사항', 'REQ-001');
  p.relate('ST-001', '검증 요구사항', 'REQ-001');
  p.relate('CR-001', '영향 요구사항', 'REQ-001');
  p.relate('CR-001', '영향 설계 사항', 'DSN-001');
  p.complete();
  p.start(2, '요구사항 쓰기');
  return p;
}

function repoRoutes(p, repo = 'o/n', ref = 'v1', file = '.kiotty/spec.kisp') {
  return { [`${RAW}/${repo}/${ref}/${file}`]: { status: 200, body: p.bytes() } };
}

async function openRepo(p, query = '?repo=o/n&ref=v1') {
  const net = createFetch(repoRoutes(p));
  const viewer = await openViewer({ url: `/${query}`, fetch: net.fetch });
  return { viewer, net };
}

const ids = (rows) => rows.map((r) => r.id);
const dashed = (text) => (text === '-' ? '' : text);

// ST-045 설치 없이 브라우저에서 사양 기록을 본다 (REQ-052)
test('ST-045 viewer', { skip }, async (t) => {
  const p = richProject();
  t.after(() => p.remove());
  const before = p.hash();
  const { viewer } = await openRepo(p);

  await t.test('1 open a repo link and compare the requirement table', () => {
    assert.equal(viewer.location.hash, '#/progress');
    assert.match(viewer.html(), /<section class="card current">/, 'progress screen is shown');
    const rows = tableRows(viewer.go('#/items/requirement'));
    const expected = p.rows('item', 'list', '--kind', 'requirement', '--tags');
    assert.deepEqual(new Set(ids(rows)), new Set(ids(expected)), 'same IDs as ks item list');
    for (const row of expected) {
      const shown = rows.find((r) => r.id === row.id);
      assert.equal(shown.title, row.title);
      assert.equal(shown.tags[KIND_TAG], dashed(row[KIND_TAG]), `${row.id} tag`);
    }
  });

  await t.test('2 every screen has no form outside the open screen', () => {
    const kinds = viewer.specState.query('overview').kinds.map((k) => k.kind);
    const items = p.rows('item', 'list');
    const hashes = ['#/overview', '#/progress', '#/changes', ...kinds.map((k) => `#/items/${k}`), ...items.map((i) => `#/item/${i.id}`)];
    for (const hash of hashes) {
      const html = viewer.go(hash);
      assert.doesNotMatch(html, /<form/, `${hash} has no form`);
      const controls = [...html.matchAll(/<(input|select|textarea)\b([^>]*)>/g)].map((m) => m[2]);
      for (const attrs of controls) {
        assert.match(attrs, /data-act="(q|kind|cpstep)"|data-tag-name=/, `${hash}: only search, tag and step filters (${attrs.trim()})`);
      }
    }
    assert.match(viewer.go('#/open'), /<form data-form="open"/, 'open screen keeps its form');
    assert.equal(p.hash(), before, 'spec file is unchanged');
  });
});

// ST-046 공개 저장소의 사양 파일을 링크로 연다 (REQ-053)
test('ST-046 viewer link', { skip }, async (t) => {
  const p = richProject();
  t.after(() => p.remove());

  await t.test('1 open by repo and ref', async () => {
    const { viewer, net } = await openRepo(p);
    assert.equal(viewer.location.hash, '#/progress');
    assert.match(viewer.html(), /o\/n <b>@<\/b> v1/, 'header shows o/n @ v1');
    assert.deepEqual(net.calls, [`${RAW}/o/n/v1/.kiotty/spec.kisp`]);
  });

  await t.test('2 open another path', async () => {
    const net = createFetch(repoRoutes(p, 'o/n', 'v1', 'docs/a.kisp'));
    const viewer = await openViewer({ url: '/?repo=o/n&ref=v1&path=docs/a.kisp', fetch: net.fetch });
    assert.equal(viewer.specState.get().project.name, 'demo', 'that file is opened');
    assert.deepEqual(net.calls, [`${RAW}/o/n/v1/docs/a.kisp`]);
  });

  await t.test('3 missing repo, ref and path are told apart', async () => {
    const cases = [
      { query: '?repo=o/x&ref=v1', routes: { [`${API}/repos/o/x`]: { status: 404 } }, text: '저장소 o/x 을 찾지 못했습니다', value: 'o/x' },
      {
        query: '?repo=o/n&ref=v9',
        routes: { [`${API}/repos/o/n`]: { status: 200, body: '{}' }, [`${API}/repos/o/n/commits/v9`]: { status: 404 } },
        text: '버전 v9 를 찾지 못했습니다', value: 'v9',
      },
      {
        query: '?repo=o/n&ref=v1&path=nope.kisp',
        routes: { [`${API}/repos/o/n`]: { status: 200, body: '{}' }, [`${API}/repos/o/n/commits/v1`]: { status: 200, body: '{}' } },
        text: 'v1 에 nope.kisp 가 없습니다', value: 'nope.kisp',
      },
    ];
    for (const c of cases) {
      const viewer = await openViewer({ url: `/${c.query}`, fetch: createFetch(c.routes).fetch });
      const html = viewer.html();
      assert.equal(viewer.location.hash, '#/open', c.query);
      assert.ok(viewer.text().includes(c.text), `${c.query}: ${c.text}`);
      assert.ok(viewer.text().includes('로컬 파일로 여세요'), `${c.query}: local file hint`);
      assert.ok(html.includes(`value="${c.value}"`), `${c.query}: form keeps ${c.value}`);
    }
  });

  await t.test('4 text that is not a spec file', async () => {
    const net = createFetch({ [`${RAW}/o/n/v1/.kiotty/spec.kisp`]: { status: 200, body: 'hello\nworld\n' } });
    const viewer = await openViewer({ url: '/?repo=o/n&ref=v1', fetch: net.fetch });
    assert.equal(viewer.specState.get().project, null, 'nothing is opened');
    assert.match(viewer.text(), /사양 파일 형식이 아닙니다 \(\d+ 번째 줄\)/);
  });

  await t.test('5 no repo shows the open screen', async () => {
    const net = createFetch();
    const viewer = await openViewer({ url: '/', fetch: net.fetch });
    assert.match(viewer.html(), /<form data-form="open"/, 'repo input');
    assert.match(viewer.html(), /data-act="pick"/, 'local file open');
    assert.deepEqual(net.calls, []);
  });
});

// ST-047 내 PC 의 사양 파일을 끌어 놓아 연다 (REQ-054)
test('ST-047 viewer local', { skip }, async (t) => {
  const p = richProject();
  const bad = createProject('bad');
  writeFileSync(`${bad.dir}/bad.kisp`, 'hello\nworld\n');
  t.after(() => { p.remove(); bad.remove(); });
  const net = createFetch();
  const store = createHandleStore();
  const viewers = [];
  t.after(() => { for (const v of viewers) v.close(); });
  const open = async (options = {}) => {
    const viewer = await openViewer({ fetch: net.fetch, handleStore: store, ...options });
    viewers.push(viewer);
    return viewer;
  };
  const designCount = (viewer) => tableRows(viewer.go('#/items/design')).length;

  await t.test('1 drop a file with a handle', async () => {
    const viewer = await open();
    await viewer.app.openDropped(dropOf(fileHandle(p.file)));
    assert.equal(viewer.location.hash, '#/progress');
    assert.ok(viewer.text().includes('📄 spec.kisp'), 'file name in the header');
    assert.ok(viewer.text().includes('자동 갱신'), 'auto reload badge');
    assert.ok(!viewer.text().includes('자동 갱신 안 됨'));
  });

  await t.test('2 pick a file', async () => {
    const viewer = await open({ showOpenFilePicker: async () => [fileHandle(p.file)] });
    viewer.click({ act: 'pick' });
    assert.ok(await waitFor(() => viewer.specState.get().project), 'opened by the picker');
    assert.ok(viewer.text().includes('📄 spec.kisp'));
  });

  await t.test('3 a bad file keeps the open one', async () => {
    const viewer = await open();
    await viewer.app.openDropped(dropOf(fileHandle(p.file)));
    await viewer.app.openDropped(dropOf(fileHandle(`${bad.dir}/bad.kisp`)));
    assert.ok(viewer.text().includes('bad.kisp: 열지 못했습니다 — 사양 파일 형식이 아닙니다'), 'notice');
    assert.equal(viewer.specState.get().source.name, 'spec.kisp', 'previous file stays');
    assert.equal(viewer.specState.get().project.name, 'demo');
  });

  await t.test('4 local files are never sent', () => {
    assert.deepEqual(net.calls, [], 'fetch is never called');
  });

  await t.test('5 a change is reloaded within 2 seconds and the address stays', async () => {
    const viewer = await open();
    await viewer.app.openDropped(dropOf(fileHandle(p.file)));
    viewer.go('#/item/REQ-001');
    const before = designCount(viewer);
    viewer.go('#/item/REQ-001');
    const revision = viewer.specState.get().revision;
    await sleep(1100);
    p.add('design', '새 설계');
    assert.ok(await waitFor(() => viewer.specState.get().revision > revision, 2000), 'reloaded within 2 seconds');
    assert.equal(viewer.location.hash, '#/item/REQ-001', 'address stays');
    assert.equal(designCount(viewer), before + 1, 'new design is in the table');
  });

  await t.test('6 a broken write keeps the old content until it is fixed', async () => {
    const viewer = await open();
    await viewer.app.openDropped(dropOf(fileHandle(p.file)));
    const good = p.bytes();
    const count = designCount(viewer);
    await sleep(1100);
    writeFileSync(p.file, 'broken\n');
    assert.ok(await waitFor(() => viewer.specState.get().watch?.error, 2000), 'reload fails');
    assert.ok(viewer.text().includes('다시 읽지 못했습니다'));
    assert.equal(designCount(viewer), count, 'old content stays');
    const revision = viewer.specState.get().revision;
    writeFileSync(p.file, good);
    p.add('design', '고친 뒤 설계');
    assert.ok(await waitFor(() => viewer.specState.get().revision > revision, 2000), 'reloaded after the fix');
    assert.ok(!viewer.text().includes('다시 읽지 못했습니다'), 'error is gone');
    assert.equal(designCount(viewer), count + 1, 'new content');
  });

  await t.test('7 a kept handle reopens after a refresh', async () => {
    await store.save(fileHandle(p.file));
    const viewer = await open();
    assert.equal(viewer.specState.get().source?.name, 'spec.kisp', 'granted handle opens at start');

    await store.save(fileHandle(p.file, { query: 'prompt' }));
    const asked = await open();
    assert.equal(asked.specState.get().project, null);
    assert.match(asked.html(), /data-act="reopen">다시 열기: spec\.kisp</, 'reopen button with file name');
    asked.click({ act: 'reopen' }, 'button');
    assert.ok(await waitFor(() => asked.specState.get().project), 'reopened');
  });

  await t.test('8 a drop without a handle is not watched', async () => {
    const viewer = await open();
    await viewer.app.openDropped(dropOf(null, fileSnapshot(p.file)));
    assert.ok(viewer.text().includes('자동 갱신 안 됨 — 바뀌면 다시 고르세요'));
    const count = designCount(viewer);
    p.add('design', '안 보일 설계');
    await sleep(1500);
    assert.equal(designCount(viewer), count, 'old content stays');
  });
});

// ST-048 목적 · 단계와 지금 진행 상황을 본다 (REQ-055)
test('ST-048 viewer progress', { skip }, async (t) => {
  const p = createProject('demo');
  const empty = createProject('empty');
  t.after(() => { p.remove(); empty.remove(); });
  p.start(1, '변경 요청 받기');
  p.complete();
  p.start(2, '요구사항 쓰기');
  p.add('requirement', '로그인한다', { tags: [`${KIND_TAG}=기능 요구사항`] });

  function compareWithKs(viewer) {
    const html = viewer.go('#/progress');
    const cps = p.rows('checkpoint', 'list');
    const shown = [...html.matchAll(/<tr><td data-l="CP"><b>([^<]*)<\/b><\/td><td data-l="단계">([^<]*)<\/td><td data-l="상태"><span[^>]*>([^<]*)<\/span><\/td><td data-l="설명">([^<]*)<\/td>/g)]
      .map((m) => m.slice(1, 5));
    const expected = cps.map((c) => [c.checkpoint, c.step, STATE_NAMES[c.state], c.description]).reverse();
    assert.deepEqual(shown, expected, 'checkpoint list is the same as ks (newest first, UI-172)');
    const next = p.ks('next');
    const unmet = [...next.matchAll(/^([A-Z]+-\d+)\t[a-z_]+\t(.+)$/gm)].map((m) => [m[1], m[2]]);
    const rows = [...html.matchAll(/<td data-l="ID"><a class="iid" href="[^"]*">([^<]*)<\/a><\/td>.*?<span class="warn">([^<]*)<\/span>/g)].map((m) => [m[1], m[2]]);
    assert.deepEqual(rows, unmet, 'unmet items are the same as ks next');
    const head = /^(CP-\d+)\s+(\d+) (\S+)\s+(\S+(?: \S+)?)$/m.exec(next);
    assert.match(html, new RegExp(`<h2>${head[1]}</h2><span class="pill">${head[2]} ${head[3]}</span><span class="pill state-[a-z]+">${head[4]}</span>`), 'current checkpoint');
    return { html, unmet };
  }

  await t.test('1 current step and unmet item', async () => {
    const { viewer } = await openRepo(p);
    const html = viewer.go('#/progress');
    assert.match(html, /<li class="on"><span>2<\/span>요구사항<\/li>/, 'step 2 of 12');
    assert.equal([...html.matchAll(/<ol class="steps">.*?<\/ol>/gs)][0][0].match(/<li/g).length, 12);
    assert.match(html, />REQ-001<\/a><\/td>.*?인수 조건 없음/s);
  });

  await t.test('2 same as ks next and ks checkpoint list', async () => {
    const { viewer } = await openRepo(p);
    assert.equal(compareWithKs(viewer).unmet.length, 1);
    p.ks('item', 'set', 'REQ-001', '--body', '## 인수 조건\n- 로그인된다\n');
    const again = await openRepo(p);
    assert.equal(compareWithKs(again.viewer).unmet.length, 0);
    assert.ok(again.viewer.text().includes('사용자 확정을 기다립니다'));
  });

  await t.test('3 a file without checkpoints', async () => {
    const { viewer } = await openRepo(empty);
    assert.ok(textOf(viewer.go('#/progress')).includes('진행 기록이 없습니다'));
    assert.match(viewer.go('#/overview'), /<h1>empty<\/h1>/, 'overview opens');
    assert.ok(textOf(viewer.go('#/items/requirement')).includes('이 종류의 항목이 없습니다'), 'item table opens');
  });

  await t.test('4 overview has purpose, roles, 12 steps and counts', async () => {
    const { viewer } = await openRepo(p);
    const html = viewer.go('#/overview');
    assert.match(html, /<h2>목적<\/h2><div class="pre">1\. 라이프사이클/);
    assert.match(html, /<h2>역할<\/h2><div class="pre">- AI:/);
    assert.equal(html.match(/<td data-l="단계">/g).length, STEPS.length);
    const counts = Object.fromEntries([...html.matchAll(/href="#\/items\/([a-z_0-9]+)" class="count[^"]*"><b>(\d+)<\/b>/g)].map((m) => [m[1], Number(m[2])]));
    for (const kind of Object.keys(counts)) {
      assert.equal(counts[kind], p.rows('item', 'list', '--kind', kind).length, `${kind} count`);
    }
    assert.ok(Object.keys(counts).length >= 14);
  });
});

// ST-049 항목을 종류별 표로 보고 걸러 찾는다 (REQ-056)
test('ST-049 viewer items', { skip }, async (t) => {
  const p = richProject();
  p.add('requirement', '본문 시험', {
    body: '## 진술\n- 하나\n- 둘\n\n| 이름 | 값 |\n| --- | --- |\n| a | b |\n\n```\ncode <b>x</b>\n```\n<script>alert(1)</script> <img src=x onerror=alert(1)>\n',
  });
  t.after(() => p.remove());
  const { viewer } = await openRepo(p);

  await t.test('1 rows in ID order, children under the parent', () => {
    const rows = tableRows(viewer.go('#/items/requirement'));
    assert.deepEqual(ids(rows), ['REQ-001', 'REQ-004', 'REQ-002', 'REQ-003', 'REQ-005']);
    for (const row of p.rows('item', 'list', '--kind', 'requirement', '--tags')) {
      const shown = rows.find((r) => r.id === row.id);
      assert.deepEqual([shown.title, shown.summary, shown.tags[KIND_TAG]], [row.title, dashed(row.summary), dashed(row[KIND_TAG])], row.id);
    }
    assert.match(viewer.html(), /indent-1"><span class="caret">▸<\/span> <a class="iid" href="#\/item\/REQ-004"/, 'child is indented');
  });

  await t.test('2 filter by a tag value', () => {
    viewer.go('#/items/requirement');
    viewer.change({ tagName: KIND_TAG, tagValue: '환경 요구사항' });
    const expected = p.rows('item', 'list', '--kind', 'requirement', '--tag', `${KIND_TAG}=환경 요구사항`);
    assert.deepEqual(new Set(ids(tableRows(viewer.html()))), new Set(ids(expected)));
    assert.match(viewer.html(), new RegExp(`count-badge">${expected.length} / 5<`));
  });

  await t.test('3 search title and summary ignoring case', () => {
    viewer.go('#/items/requirement');
    viewer.input({ act: 'q' }, 'LOGIN');
    assert.deepEqual(ids(tableRows(viewer.html())), ['REQ-001']);
    viewer.input({ act: 'q' }, '로그인한다');
    assert.deepEqual(ids(tableRows(viewer.html())), ['REQ-001'], 'summary is searched');
  });

  await t.test('4 an opened body draws markdown and keeps HTML as text', () => {
    viewer.go('#/items/requirement');
    viewer.click({ toggle: 'REQ-005' }, 'tr');
    const html = viewer.html();
    const body = /data-md="REQ-005"[^>]*>(.*?)<a class="more"/s.exec(html)[1];
    assert.match(body, /<ul><li>하나<\/li><li>둘<\/li><\/ul>/, 'list');
    assert.match(body, /<table class="md">/, 'table');
    assert.match(body, /<pre>code &lt;b&gt;x&lt;\/b&gt;<\/pre>/, 'code block');
    assert.doesNotMatch(body, /<script|<img/, 'no script or img element');
    assert.ok(body.includes('&lt;script&gt;alert(1)&lt;/script&gt;') && body.includes('&lt;img src=x onerror=alert(1)&gt;'), 'shown as text');
  });

  await t.test('5 a kind without items', () => {
    assert.ok(textOf(viewer.go('#/items/milestone')).includes('이 종류의 항목이 없습니다'));
  });
});

// ST-050 항목 하나의 추적 관계를 따라간다 (REQ-057)
test('ST-050 viewer trace', { skip }, async (t) => {
  const p = richProject();
  t.after(() => p.remove());
  const { viewer } = await openRepo(p);

  function relationsOf(id) {
    const rows = p.rows('relation', 'list', id);
    const group = (list, key) => list.reduce((acc, r) => ({ ...acc, [r.relation]: [...(acc[r.relation] ?? []), r[key]] }), {});
    return { out: group(rows.filter((r) => r.from === id), 'to'), in: group(rows.filter((r) => r.to === id), 'from') };
  }
  const sort = (groups) => Object.fromEntries(Object.entries(groups).map(([k, v]) => [k, [...v].sort()]));

  await t.test('1 relations by name are the same as ks relation list', () => {
    for (const id of ['REQ-001', 'DSN-001']) {
      const sections = relationSections(viewer.go(`#/item/${id}`));
      const expected = relationsOf(id);
      const out = sections['이 항목이 가리키는 것'];
      assert.deepEqual(sort(Array.isArray(out) ? {} : out), sort(expected.out), `${id} out`);
      const into = sections['이 항목을 가리키는 것'];
      assert.deepEqual(sort(Array.isArray(into) ? {} : into), sort(expected.in), `${id} in`);
    }
  });

  await t.test('2 relation links open the item', () => {
    const html = viewer.go('#/item/REQ-001');
    const links = [...html.matchAll(/<div class="relgroup">.*?<\/div><\/div>|<div class="relgroup">.*?<\/ul><\/div>/gs)]
      .flatMap((m) => [...m[0].matchAll(/href="(#\/item\/[^"]+)">([^<]+)</g)]);
    assert.ok(links.length >= 3);
    for (const [, href, id] of links) {
      assert.equal(href, `#/item/${id}`);
      assert.match(viewer.go(href), new RegExp(`<span class="iid big">${id}</span>`), `${href} opens`);
    }
  });

  await t.test('3 impact is the same as ks impact', () => {
    const sections = relationSections(viewer.go('#/item/REQ-001'));
    assert.deepEqual([...sections['영향 항목']].sort(), ids(p.rows('impact', 'REQ-001')).sort());
  });

  await t.test('4 open by address, and a missing item', () => {
    assert.match(viewer.go('#/item/DSN-001'), /<h1>로그인 모듈<\/h1>/);
    const html = viewer.go('#/item/NOPE-9');
    assert.ok(textOf(html).includes('없는 항목: NOPE-9'));
    assert.match(html, /href="#\/items\/[a-z_]+">항목 표로</);
  });
});

// ST-051 변경 요청과 영향 항목을 본다 (REQ-058)
test('ST-051 viewer changes', { skip }, async (t) => {
  const p = richProject();
  for (let k = 1; k <= 6; k += 1) p.add('change_request', `끝난 변경 ${k}`, { tags: ['상태=완료', '진입 단계=Step 2'] });
  p.add('change_request', '거부한 변경', { tags: ['상태=거부', '진입 단계=Step 2'] });
  t.after(() => p.remove());
  const { viewer } = await openRepo(p);

  const columns = (html) => [...html.matchAll(/<section class="kcol kcol-([a-z]+)">\s*<header><h2>([^<]*)<\/h2>(.*?)<\/section>/gs)]
    .map(([, key, title, body]) => ({ key, title, ids: [...body.matchAll(/<span class="iid">([^<]+)<\/span>/g)].map((m) => m[1]), body }));
  const ksIds = (...values) => values.flatMap((v) => ids(p.rows('item', 'list', '--kind', 'change_request', '--tag', `상태=${v}`)));

  await t.test('1 columns in order with the same cards as ks', () => {
    const cols = columns(viewer.go('#/changes'));
    assert.deepEqual(cols.map((c) => c.title), ['백로그', '열림 · 반영 중', '완료', '거부']);
    assert.deepEqual(new Set(cols[0].ids), new Set(ksIds('백로그')));
    assert.deepEqual(new Set(cols[1].ids), new Set(ksIds('열림', '반영 중')));
    assert.deepEqual(new Set(cols[3].ids), new Set(ksIds('거부')));
    const done = ksIds('완료');
    assert.equal(cols[2].ids.length, 5, 'five cards');
    assert.ok(cols[2].ids.every((id) => done.includes(id)));
    assert.match(cols[2].body, /data-more="done">1 개 더 보기</);
    viewer.click({ more: 'done' }, 'button');
    assert.deepEqual(new Set(columns(viewer.html())[2].ids), new Set(done), 'all after more');
  });

  await t.test('2 a card opens the change request with its affected items', () => {
    const href = /<a class="kcard" href="(#\/item\/CR-001)">/.exec(viewer.go('#/changes'))[1];
    const html = viewer.go(href);
    assert.match(html, /data-md="CR-001"[^>]*><p>로그인을 더한다<\/p>/, 'body');
    const out = relationSections(html)['영향 항목 (이 변경 요청이 가리키는 것)'];
    const rows = p.rows('relation', 'list', 'CR-001').filter((r) => r.from === 'CR-001');
    const expected = rows.reduce((acc, r) => ({ ...acc, [r.relation]: [...(acc[r.relation] ?? []), r.to] }), {});
    assert.deepEqual(out, expected);
  });
});
