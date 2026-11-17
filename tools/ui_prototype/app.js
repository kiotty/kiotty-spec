// kiotty-spec browser viewer prototype (CR-019). Screens follow UI-168 .. UI-175.
// Data: #spec comes from ks (GUI view items, models, guide), #dummy is dummy project data.
'use strict';
(function () {
  const SPEC = JSON.parse(document.getElementById('spec').textContent);
  const DUMMY = JSON.parse(document.getElementById('dummy').textContent);
  const KINDS = SPEC.kinds;
  const KIND = Object.fromEntries(KINDS.map((k) => [k.kind, k]));
  const STEP_NAME = Object.fromEntries(SPEC.steps.map((s) => [s.step, s.name]));
  const GUI = Object.fromEntries(SPEC.gui.map((g) => [g.id, g]));
  const SCREEN_UI = { open: 'UI-170', overview: 'UI-171', progress: 'UI-172', items: 'UI-173', item: 'UI-174', changes: 'UI-175' };
  const CP_STATE = { running: '진행 중', started: '시작', done: '확정' };
  const CR_COLUMNS = [
    { key: 'backlog', title: '백로그', values: ['백로그'] },
    { key: 'active', title: '열림 · 반영 중', values: ['열림', '반영 중'], showState: true },
    { key: 'done', title: '완료', values: ['완료'] },
    { key: 'rejected', title: '거부', values: ['거부'] },
  ];
  const CARD_LIMIT = 5;

  const app = document.getElementById('app');
  const S = {
    data: null, source: null, loading: false, error: null, notice: null,
    openForm: { repo: '', ref: '', path: '' },
    expanded: new Set(), moreCols: new Set(), specOpen: false, specTab: null, cpStep: '', demo: 'normal',
  };
  let byId = new Map();

  // ---------- helpers ----------
  const esc = (s) => String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  const dash = (s) => (s && s !== '-' ? s : '');
  const fmtTime = (sec) => { const d = new Date(sec * 1000); const p = (n) => String(n).padStart(2, '0'); return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`; };
  const idNum = (id) => { const m = id.match(/^([A-Z0-9]+?)-(\d+)$/); return m ? [m[1], Number(m[2])] : [id, 0]; };
  const byIdOrder = (a, b) => { const [pa, na] = idNum(a.id); const [pb, nb] = idNum(b.id); return pa === pb ? na - nb : pa < pb ? -1 : 1; };
  const itemLink = (id) => (byId.has(id) ? `<a href="#/item/${encodeURIComponent(id)}" class="iid">${esc(id)}</a>` : `<span class="iid">${esc(id)}</span>`);
  const itemRef = (id) => { const it = byId.get(id); return `${itemLink(id)}${it ? ` <span class="ititle">${esc(it.title)}</span>` : ''}`; };
  const tagsOf = (it, name) => (it.tags && it.tags[name]) || [];

  function inline(s, linkIds) {
    let h = esc(s);
    h = h.replace(/`([^`]+)`/g, '<code>$1</code>');
    h = h.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    if (linkIds) h = h.replace(/\b([A-Z][A-Z0-9]*-\d{3,4})\b/g, (m) => (byId.has(m) ? itemLink(m) : GUI[m] ? `<a href="#" data-spec="${m}">${m}</a>` : m));
    return h;
  }
  // Markdown subset. Everything is escaped first: HTML in a body shows as text.
  function markdown(md, opts = {}) {
    const lines = String(md || '').replace(/\r/g, '').split('\n');
    const out = [];
    let i = 0;
    const isBlock = (l) => /^(```|#{1,4}\s|\s*\||\s*([-*]|\d+\.)\s)/.test(l);
    while (i < lines.length) {
      const line = lines[i];
      const fence = line.match(/^```\s*(\S*)/);
      if (fence) {
        const buf = []; i++;
        while (i < lines.length && !/^```/.test(lines[i])) buf.push(lines[i++]);
        i++;
        if (fence[1] === 'wireframe' && opts.wireframe) out.push(opts.wireframe());
        else out.push(`<pre>${esc(buf.join('\n'))}</pre>`);
        continue;
      }
      const h = line.match(/^(#{1,4})\s+(.*)/);
      if (h) { const n = Math.min(6, h[1].length + 2); out.push(`<h${n}>${inline(h[2], opts.links)}</h${n}>`); i++; continue; }
      if (/^\s*\|/.test(line)) {
        const rows = [];
        while (i < lines.length && /^\s*\|/.test(lines[i])) rows.push(lines[i++]);
        const cells = (r) => r.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
        const body = rows.filter((r, k) => !(k === 1 && /^[\s|:-]+$/.test(r)));
        const [head, ...rest] = body;
        out.push('<div class="scroll"><table class="md"><thead><tr>' + cells(head).map((c) => `<th>${inline(c, opts.links)}</th>`).join('') + '</tr></thead><tbody>' +
          rest.map((r) => '<tr>' + cells(r).map((c) => `<td>${inline(c, opts.links)}</td>`).join('') + '</tr>').join('') + '</tbody></table></div>');
        continue;
      }
      if (/^\s*([-*]|\d+\.)\s+/.test(line)) {
        const ordered = /^\s*\d+\./.test(line); const items = [];
        while (i < lines.length && /^\s*([-*]|\d+\.)\s+/.test(lines[i])) items.push(lines[i++].replace(/^\s*([-*]|\d+\.)\s+/, ''));
        const t = ordered ? 'ol' : 'ul';
        out.push(`<${t}>` + items.map((x) => `<li>${inline(x, opts.links)}</li>`).join('') + `</${t}>`);
        continue;
      }
      if (line.trim() === '') { i++; continue; }
      const para = [];
      while (i < lines.length && lines[i].trim() !== '' && !isBlock(lines[i])) para.push(lines[i++]);
      out.push(`<p>${inline(para.join(' '), opts.links)}</p>`);
    }
    return out.join('\n');
  }

  // ---------- routing ----------
  function route() {
    const h = decodeURI(location.hash.replace(/^#\/?/, ''));
    const [p, qs] = h.split('?');
    const parts = p.split('/').filter(Boolean);
    const params = new URLSearchParams(qs || '');
    return { screen: parts[0] || 'progress', arg: parts[1] ? decodeURIComponent(parts[1]) : '', params };
  }
  const go = (hash) => { location.hash = hash; };
  function itemsHash(kind, q, tags) {
    const p = new URLSearchParams();
    for (const t of tags) p.append('tag', t);
    if (q) p.set('q', q);
    const s = p.toString();
    return `#/items/${kind}${s ? '?' + s : ''}`;
  }

  // ---------- data ----------
  function load(source, delay = 450) {
    S.loading = true; S.error = null; render();
    setTimeout(() => {
      const d = JSON.parse(JSON.stringify(DUMMY));
      if (S.demo === 'nocp') { d.checkpoints = []; d.next = { work: [], actions: [{ text: '체크포인트를 시작한다', command: 'ks checkpoint start 1 --description "..."' }] }; }
      if (S.demo === 'nocr') { d.items = d.items.filter((x) => x.kind !== 'change_request'); }
      S.data = d; S.source = source; S.loading = false;
      byId = new Map(d.items.map((x) => [x.id, x]));
      S.expanded.clear();
      const r = route();
      if (r.screen === 'open' || !location.hash) go('#/progress'); else render();
    }, delay);
  }
  function failOpen(msg) { S.loading = false; S.error = msg; if (route().screen !== 'open') go('#/open'); else render(); }

  function openRemote(repo, ref, filePath) {
    const src = DUMMY.source;
    const local = ' 비공개 저장소라면 아래에서 로컬 파일로 여세요.';
    S.openForm = { repo, ref, path: filePath };
    S.loading = true; S.error = null; render();
    setTimeout(() => {
      if (repo !== src.repo) return failOpen(`저장소 ${repo} 을 찾지 못했습니다 (없거나 비공개).${local}`);
      if (ref && ref !== src.ref && ref !== 'main') return failOpen(`버전 ${ref} 를 찾지 못했습니다.${local}`);
      if (filePath && filePath !== src.path) return failOpen(`${ref || 'main'} 에 ${filePath} 가 없습니다.`);
      load({ repo, ref: ref || 'main', path: filePath || src.path }, 0);
    }, 450);
  }
  function openFiles(files) {
    if (!files || !files.length) return;
    const f = files[0];
    if (files.length > 1) toast('파일 하나만 엽니다');
    const reader = new FileReader();
    reader.onload = () => {
      const first = String(reader.result).split('\n')[0];
      if (!/^#kisp \d+ model \d+/.test(first)) {
        const msg = '사양 파일 형식이 아닙니다 (1 번째 줄)';
        if (S.data) toast(msg, true); else failOpen(msg);
        return;
      }
      toast('프로토타입: 파일 내용 대신 더미 데이터를 보여 줍니다');
      load({ file: f.name });
    };
    reader.readAsText(f.slice(0, 256));
  }

  // ---------- shell ----------
  let toastTimer = 0;
  function toast(msg, bad) {
    let t = document.getElementById('toast');
    if (!t) { t = document.createElement('div'); t.id = 'toast'; document.body.appendChild(t); }
    t.className = 'toast show' + (bad ? ' bad' : '');
    t.textContent = msg;
    clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.className = 'toast'; }, 2600);
  }
  function sourceBadge() {
    if (!S.source) return '';
    if (S.source.file) return `<span class="src" title="로컬 파일">📄 ${esc(S.source.file)}</span>`;
    return `<span class="src" title="${esc(S.source.path)}">${esc(S.source.repo)} <b>@</b> ${esc(S.source.ref)}</span>`;
  }
  function header(screen) {
    const tabs = [['overview', '개요'], ['progress', '진행'], ['items', '항목'], ['changes', '변경 요청']];
    const tabHref = (k) => (k === 'items' ? `#/items/${S.lastKind || 'requirement'}` : `#/${k}`);
    const active = screen === 'item' ? '' : screen;
    return `
<header class="top">
  <div class="topbar">
    <a class="brand" href="#/progress">kiotty-spec <span>뷰어</span></a>
    ${sourceBadge()}
    <span class="grow"></span>
    <a class="btn ghost" href="#/open" title="다른 파일 열기">다른 파일 열기</a>
    <button class="btn ghost" data-act="spec" title="이 화면의 ks 명세">명세</button>
  </div>
  ${S.data ? `<nav class="tabs">${tabs.map(([k, t]) => `<a href="${tabHref(k)}" class="${active === k ? 'on' : ''}">${t}</a>`).join('')}</nav>` : ''}
  ${S.loading ? '<div class="loadbar"><i></i></div>' : ''}
</header>`;
  }
  function demoBar() {
    const opts = [['normal', '정상 데이터'], ['nocp', '진행 기록 없음'], ['nocr', '변경 요청 없음'], ['nowasm', 'WebAssembly 미지원']];
    return `<div class="demo"><span>프로토타입 · 더미 데이터</span>
      <select data-act="demo" aria-label="데모 상태">${opts.map(([v, t]) => `<option value="${v}" ${S.demo === v ? 'selected' : ''}>${t}</option>`).join('')}</select>
      <a href="viewer_interface.html" target="_blank">인터페이스 명세</a></div>`;
  }
  function specPanel(screen) {
    if (!S.specOpen) return '';
    const cur = SCREEN_UI[screen] || 'UI-168';
    const tab = S.specTab && GUI[S.specTab] ? S.specTab : cur;
    const ids = Array.from(new Set([cur, 'UI-168', 'UI-169', tab])).filter((x) => GUI[x]);
    const g = GUI[tab];
    const body = markdown(g.body, { links: true, wireframe: () => `<p class="muted">화면 그림: <a href="viewer_interface.html#${tab}" target="_blank">viewer_interface.html#${tab}</a></p>` });
    return `<div class="scrim" data-act="spec"></div>
<aside class="spec" aria-label="명세">
  <div class="spechead"><b>ks 명세</b><span class="grow"></span><button class="btn ghost" data-act="spec">닫기</button></div>
  <div class="spectabs">${ids.map((x) => `<button class="${x === tab ? 'on' : ''}" data-spec="${x}">${x} ${esc(GUI[x].title)}</button>`).join('')}</div>
  <div class="specbody"><h2>${tab} ${esc(g.title)}</h2><p class="muted">${esc(dash(g.summary))}</p>${body}</div>
</aside>`;
  }

  // ---------- screens ----------
  function screenOpen() {
    const f = S.openForm;
    return `<main class="page narrow">
  ${S.error ? `<div class="alert">${esc(S.error)}</div>` : ''}
  <section class="card">
    <h2>공개 저장소에서 열기</h2>
    <form data-form="open" class="form" novalidate>
      <label>저장소<input name="repo" placeholder="OWNER/NAME" value="${esc(f.repo)}" autocomplete="off"></label>
      <label>버전<input name="ref" placeholder="태그 · 브랜치 · commit (비우면 기본 브랜치)" value="${esc(f.ref)}" autocomplete="off"></label>
      <details ${f.path ? 'open' : ''}><summary>파일 위치</summary><label><input name="path" placeholder=".kiotty/spec.kisp" value="${esc(f.path)}" autocomplete="off"></label></details>
      <div class="formerr" id="formerr"></div>
      <div><button class="btn" type="submit">열기</button> <span class="muted">예: ${esc(DUMMY.source.repo)} · ${esc(DUMMY.source.ref)}</span></div>
    </form>
  </section>
  <section class="card">
    <h2>내 PC 의 파일 열기</h2>
    <label class="drop" id="drop"><input type="file" accept=".kisp" hidden data-act="file"><span>.kisp 파일을 여기에 끌어 놓으세요</span><span class="btn">파일 고르기</span></label>
    <p class="muted">파일은 브라우저 안에서만 읽고 어디에도 보내지 않습니다.</p>
  </section>
</main>`;
  }

  function screenOverview() {
    const d = S.data;
    const counts = KINDS.map((k) => ({ k, n: d.items.filter((x) => x.kind === k.kind).length }));
    return `<main class="page">
  <section class="hero">
    <h1>${esc(d.project.name || (S.source && (S.source.repo || S.source.file)))}</h1>
    <p class="muted">만든 시각 ${fmtTime(d.project.createdAt)} · 항목 ${d.items.length} 개 · 체크포인트 ${d.checkpoints.length} 개</p>
  </section>
  <div class="grid2">
    <section class="card"><h2>목적</h2><div class="pre">${esc(SPEC.purpose)}</div></section>
    <section class="card"><h2>역할</h2><div class="pre">${esc(SPEC.roles)}</div>
      <h2 class="mt">항목 수</h2>
      <div class="counts">${counts.map(({ k, n }) => `<a href="#/items/${k.kind}" class="count ${n ? '' : 'zero'}"><b>${n}</b><span>${esc(k.name)}</span><small>${k.prefix}</small></a>`).join('')}</div>
    </section>
  </div>
  <section class="card"><h2>12 단계</h2>
    <table class="rt"><thead><tr><th>단계</th><th>이름</th><th>하는 일</th><th>항목</th></tr></thead><tbody>
    ${SPEC.steps.map((s) => `<tr><td data-l="단계">${s.step}</td><td data-l="이름"><b>${esc(s.name)}</b></td><td data-l="하는 일">${esc(s.does)}</td><td data-l="항목">${esc(s.kinds)}</td></tr>`).join('')}
    </tbody></table>
  </section>
</main>`;
  }

  function stepChips(cur) {
    return `<ol class="steps">${SPEC.shortSteps.map((n, i) => `<li class="${i + 1 === cur.step ? 'on' : i + 1 < cur.step ? 'past' : ''}"><span>${i + 1}</span>${esc(n)}</li>`).join('')}</ol>`;
  }
  function screenProgress() {
    const d = S.data;
    if (!d.checkpoints.length) {
      return `<main class="page"><section class="card empty"><h2>진행 기록이 없습니다</h2><p class="muted">체크포인트가 아직 없습니다. 다른 탭은 그대로 볼 수 있습니다.</p>
      ${actions(d.next.actions)}</section></main>`;
    }
    const cur = d.checkpoints[d.checkpoints.length - 1];
    const list = d.checkpoints.slice().reverse().filter((c) => !S.cpStep || String(c.step) === S.cpStep);
    return `<main class="page">
  <p class="hint">처음이면 <a href="#/overview">개요</a> 에서 이 기록이 무엇인지 보세요.</p>
  <section class="card current">
    <div class="cprow"><h2>${esc(cur.id)}</h2><span class="pill">${cur.step} ${esc(STEP_NAME[cur.step] || '')}</span><span class="pill state-${cur.state}">${esc(CP_STATE[cur.state] || cur.state)}</span><span class="muted">${fmtTime(cur.createdAt)}</span></div>
    <p>${inline(cur.description, true)}</p>
    ${stepChips(cur)}
  </section>
  ${d.next.work.length ? `<section class="card"><h2>작업할 항목 <span class="count-badge">${d.next.work.length}</span></h2>
    <table class="rt"><thead><tr><th>ID</th><th>종류</th><th>제목</th><th>이유</th></tr></thead><tbody>
    ${d.next.work.map((w) => { const it = byId.get(w.id); return `<tr><td data-l="ID">${itemLink(w.id)}</td><td data-l="종류">${esc(it ? KIND[it.kind].name : '')}</td><td data-l="제목">${esc(it ? it.title : '')}</td><td data-l="이유"><span class="warn">${esc(w.reason)}</span></td></tr>`; }).join('')}
    </tbody></table></section>` : ''}
  <section class="card"><h2>할 행동</h2>${actions(d.next.actions)}</section>
  <section class="card"><div class="cprow"><h2>체크포인트</h2><span class="grow"></span>
    <select data-act="cpstep" aria-label="단계 거르기"><option value="">모든 단계</option>${SPEC.steps.map((s) => `<option value="${s.step}" ${S.cpStep === String(s.step) ? 'selected' : ''}>${s.step} ${esc(s.name)}</option>`).join('')}</select></div>
    <table class="rt"><thead><tr><th>CP</th><th>단계</th><th>상태</th><th>설명</th><th>시각</th></tr></thead><tbody>
    ${list.map((c) => `<tr><td data-l="CP"><b>${esc(c.id)}</b></td><td data-l="단계">${c.step} ${esc(STEP_NAME[c.step] || '')}</td><td data-l="상태"><span class="pill state-${c.state}">${esc(CP_STATE[c.state] || c.state)}</span></td><td data-l="설명">${inline(c.description, true)}</td><td data-l="시각" class="nowrap">${fmtTime(c.createdAt)}</td></tr>`).join('') || '<tr><td colspan="5" class="muted">이 단계의 체크포인트가 없습니다</td></tr>'}
    </tbody></table></section>
</main>`;
  }
  function actions(list) {
    return `<ul class="actions">${list.map((a) => `<li><div>${esc(a.text)}${a.user ? ' <span class="pill user">사용자</span>' : ''}</div>
      <div class="cmd"><code>${esc(a.command)}</code><button class="btn ghost small" data-copy="${esc(a.command)}">복사</button></div></li>`).join('')}</ul>`;
  }

  // ----- items -----
  function itemFilter(kind, params) {
    const q = (params.get('q') || '').toLowerCase();
    const tagSel = {};
    for (const t of params.getAll('tag')) { const k = t.indexOf(':'); if (k > 0) (tagSel[t.slice(0, k)] ||= []).push(t.slice(k + 1)); }
    const all = S.data.items.filter((x) => x.kind === kind).sort(byIdOrder);
    const hit = all.filter((x) => (!q || x.title.toLowerCase().includes(q) || dash(x.summary).toLowerCase().includes(q)) &&
      Object.entries(tagSel).every(([n, vals]) => tagsOf(x, n).some((v) => vals.includes(v))));
    // children right under their parent; a child whose parent is filtered out stays at its own place
    const ids = new Set(hit.map((x) => x.id));
    const ordered = [];
    for (const x of hit) {
      if (x.parent && ids.has(x.parent)) continue;
      ordered.push({ it: x, depth: 0 });
      const walk = (p, depth) => { for (const c of hit.filter((y) => y.parent === p.id)) { ordered.push({ it: c, depth }); walk(c, depth + 1); } };
      walk(x, 1);
    }
    return { all, ordered, q: params.get('q') || '', tagSel };
  }
  function screenItems(r) {
    const kind = r.arg || 'requirement';
    const model = KIND[kind];
    if (!model) return `<main class="page"><section class="card empty"><h2>없는 종류: ${esc(kind)}</h2><p><a href="#/items/requirement">요구사항 표로</a></p></section></main>`;
    S.lastKind = kind;
    const counts = Object.fromEntries(KINDS.map((k) => [k.kind, S.data.items.filter((x) => x.kind === k.kind).length]));
    const f = itemFilter(kind, r.params);
    return `<main class="page items">
  <aside class="kinds">
    <select data-act="kind" aria-label="종류">${KINDS.map((k) => `<option value="${k.kind}" ${k.kind === kind ? 'selected' : ''}>${esc(k.name)} (${counts[k.kind]})</option>`).join('')}</select>
    <ul>${KINDS.map((k) => `<li><a href="#/items/${k.kind}" class="${k.kind === kind ? 'on' : ''} ${counts[k.kind] ? '' : 'zero'}"><span>${esc(k.name)}</span><small>${counts[k.kind]}</small></a></li>`).join('')}</ul>
  </aside>
  <section class="itembox">
    <div class="ihead"><h2>${esc(model.name)}</h2><code class="muted">${kind}</code></div>
    <div class="toolbar">
      <input type="search" data-act="q" placeholder="제목 · 요약에서 찾기" value="${esc(f.q)}" aria-label="찾기">
      ${model.tags.map((t) => { const sel = f.tagSel[t.name] || []; return `<details class="filter ${sel.length ? 'set' : ''}"><summary>${esc(t.name)}${sel.length ? ': ' + esc(sel.join(', ')) : ''}</summary>
        <div class="menu">${t.values.map((v) => `<label><input type="checkbox" data-tag="${esc(t.name + ':' + v)}" ${sel.includes(v) ? 'checked' : ''}> ${esc(v)}</label>`).join('')}</div></details>`; }).join('')}
      <span class="count-badge" id="resultcount"></span>
      <a href="#/items/${kind}" class="clear" id="clearf" ${f.q || Object.keys(f.tagSel).length ? '' : 'hidden'}>거르기 지우기</a>
    </div>
    <div id="itemtable"></div>
  </section>
</main>`;
  }
  function renderItemTable() {
    const r = route(); const kind = r.arg || 'requirement'; const model = KIND[kind];
    const box = document.getElementById('itemtable'); if (!box || !model) return;
    const f = itemFilter(kind, r.params);
    document.getElementById('resultcount').textContent = `${f.ordered.length} / ${f.all.length}`;
    document.getElementById('clearf').hidden = !(f.q || Object.keys(f.tagSel).length);
    if (!f.all.length) { box.innerHTML = '<div class="empty">이 종류의 항목이 없습니다</div>'; return; }
    if (!f.ordered.length) { box.innerHTML = `<div class="empty">맞는 항목이 없습니다 · <a href="#/items/${kind}">거르기 지우기</a></div>`; return; }
    const tagCols = model.tags.map((t) => t.name);
    box.innerHTML = `<table class="rt items"><thead><tr><th>ID</th><th>제목</th><th>요약</th>${tagCols.map((t) => `<th>${esc(t)}</th>`).join('')}<th>체크포인트</th></tr></thead><tbody>
    ${f.ordered.map(({ it, depth }) => {
      const open = S.expanded.has(it.id);
      return `<tr class="row ${open ? 'open' : ''}" data-toggle="${esc(it.id)}">
        <td data-l="ID" class="nowrap" style="padding-left:${8 + depth * 18}px"><span class="caret">${open ? '▾' : '▸'}</span> <a href="#/item/${encodeURIComponent(it.id)}" class="iid">${esc(it.id)}</a></td>
        <td data-l="제목" class="ttl">${esc(it.title)}</td><td data-l="요약" class="muted">${esc(dash(it.summary))}</td>
        ${tagCols.map((t) => `<td data-l="${esc(t)}">${tagsOf(it, t).map((v) => `<span class="tag">${esc(v)}</span>`).join(' ')}</td>`).join('')}
        <td data-l="체크포인트" class="nowrap muted">${esc(it.checkpoint)}</td></tr>
      ${open ? `<tr class="bodyrow"><td colspan="${4 + tagCols.length}"><div class="md">${it.body ? markdown(it.body, { links: true }) : '<p class="muted">본문 없음</p>'}</div><a class="more" href="#/item/${encodeURIComponent(it.id)}">상세 보기 →</a></td></tr>` : ''}`;
    }).join('')}</tbody></table>`;
  }

  // ----- item detail -----
  function impactOf(id, maxDepth = 3) {
    const seen = new Map(); let frontier = [id];
    for (let depth = 1; depth <= maxDepth && frontier.length; depth++) {
      const next = [];
      for (const t of frontier) for (const [from, , to] of S.data.relations) if (to === t && from !== id && !seen.has(from)) { seen.set(from, depth); next.push(from); }
      frontier = next;
    }
    return Array.from(seen.entries()).sort((a, b) => a[1] - b[1] || byIdOrder({ id: a[0] }, { id: b[0] }));
  }
  function relGroups(list, key) {
    const g = new Map();
    for (const r of list) { if (!g.has(r[1])) g.set(r[1], []); g.get(r[1]).push(r[key]); }
    if (!g.size) return '<p class="muted">(없음)</p>';
    return Array.from(g.entries()).map(([name, ids]) => `<div class="relgroup"><div class="relname">${esc(name)} <small>${ids.length}</small></div><ul>${ids.sort().map((x) => `<li>${itemRef(x)}</li>`).join('')}</ul></div>`).join('');
  }
  function screenItem(r) {
    const it = byId.get(r.arg);
    if (!it) return `<main class="page"><section class="card empty"><h2>없는 항목: ${esc(r.arg)}</h2><p><a href="#/items/${S.lastKind || 'requirement'}">항목 표로</a></p></section></main>`;
    const model = KIND[it.kind];
    const outR = S.data.relations.filter((x) => x[0] === it.id);
    const inR = S.data.relations.filter((x) => x[2] === it.id);
    const kids = S.data.items.filter((x) => x.parent === it.id).sort(byIdOrder);
    const imp = impactOf(it.id);
    const isCR = it.kind === 'change_request';
    return `<main class="page detail">
  <div class="crumb"><a href="#/items/${it.kind}">${esc(model.name)}</a> ${it.parent ? `› ${itemLink(it.parent)}` : ''} › <b>${esc(it.id)}</b></div>
  <section class="card">
    <div class="cprow"><span class="iid big">${esc(it.id)}</span><span class="pill">${esc(model.name)}</span><span class="pill">${esc(it.checkpoint)}</span></div>
    <h1>${esc(it.title)}</h1>
    ${dash(it.summary) ? `<p class="summary">${esc(it.summary)}</p>` : ''}
    ${it.parent ? `<p class="muted">상위: ${itemRef(it.parent)}</p>` : ''}
    ${model.tags.length ? `<div class="tags">${model.tags.map((t) => `<div><span class="muted">${esc(t.name)}</span> ${tagsOf(it, t.name).map((v) => `<span class="tag">${esc(v)}</span>`).join(' ') || '<span class="muted">-</span>'}</div>`).join('')}</div>` : ''}
  </section>
  <div class="grid2 detailgrid">
    <section class="card md">${it.body ? markdown(it.body, { links: true }) : '<p class="muted">본문 없음</p>'}</section>
    <div>
      <section class="card"><h2>${isCR ? '영향 항목 (이 변경 요청이 가리키는 것)' : '이 항목이 가리키는 것'}</h2>${relGroups(outR, 2)}</section>
      <section class="card"><h2>이 항목을 가리키는 것</h2>${relGroups(inR, 0)}</section>
      <section class="card"><h2>영향 항목 <small class="muted">ks impact</small></h2>${imp.length ? `<ul class="impact">${imp.map(([x, dep]) => `<li><span class="depth">${dep}</span>${itemRef(x)}</li>`).join('')}</ul>` : '<p class="muted">(없음)</p>'}</section>
      <section class="card"><h2>하위 항목</h2>${kids.length ? `<ul class="plain">${kids.map((k) => `<li>${itemRef(k.id)}</li>`).join('')}</ul>` : '<p class="muted">(없음)</p>'}</section>
    </div>
  </div>
</main>`;
  }

  // ----- change requests -----
  function screenChanges() {
    const crs = S.data.items.filter((x) => x.kind === 'change_request').sort((a, b) => byIdOrder(b, a));
    if (!crs.length) return '<main class="page"><section class="card empty"><h2>변경 요청이 없습니다</h2></section></main>';
    const cols = CR_COLUMNS.map((g) => ({ ...g, list: crs.filter((c) => tagsOf(c, '상태').some((v) => g.values.includes(v))) }));
    const known = CR_COLUMNS.flatMap((g) => g.values);
    const none = crs.filter((c) => !tagsOf(c, '상태').some((v) => known.includes(v)));
    if (none.length) cols.push({ key: 'none', title: '상태 없음', list: none, showState: true });
    const outCount = (id) => S.data.relations.filter((r) => r[0] === id).length;
    const card = (c, showState) => {
      const meta = [...(showState ? tagsOf(c, '상태').map((v) => `<span class="tag st">${esc(v)}</span>`) : []),
        ...tagsOf(c, '출처').map((v) => `<span class="pill">${esc(v)}</span>`),
        ...tagsOf(c, '진입 단계').map((v) => `<span class="pill">${esc(v)}</span>`),
        `<span class="impactn" title="영향 항목">↳ ${outCount(c.id)}</span>`];
      return `<a class="kcard" href="#/item/${encodeURIComponent(c.id)}">
        <span class="iid">${esc(c.id)}</span><b class="ktitle">${esc(c.title)}</b>
        ${dash(c.summary) ? `<span class="ksum">${esc(c.summary)}</span>` : ''}
        <span class="kmeta">${meta.join('')}</span></a>`;
    };
    return `<main class="page wide"><div class="board">${cols.map((g) => {
      const all = S.moreCols.has(g.key);
      const shown = all ? g.list : g.list.slice(0, CARD_LIMIT);
      const rest = g.list.length - shown.length;
      return `<section class="kcol kcol-${g.key}">
        <header><h2>${esc(g.title)}</h2><span class="count-badge">${g.list.length}</span></header>
        <div class="kcards">${shown.map((c) => card(c, g.showState)).join('') || '<p class="muted kempty">(없음)</p>'}
        ${rest > 0 ? `<button class="btn ghost more-cards" data-more="${g.key}">${rest} 개 더 보기</button>` : ''}
        ${all && g.list.length > CARD_LIMIT ? `<button class="btn ghost more-cards" data-more="${g.key}">접기</button>` : ''}</div>
      </section>`;
    }).join('')}</div></main>`;
  }

  // ---------- render ----------
  function render() {
    const r = route();
    if (S.demo === 'nowasm') {
      app.innerHTML = header('') + `<main class="page narrow"><section class="card empty"><h2>이 브라우저에서는 열 수 없습니다</h2><p>WebAssembly 를 지원하는 브라우저가 필요합니다.</p><ul><li>Windows · macOS 의 크롬 · 엣지 최신 두 주 버전</li><li>macOS 의 사파리 최신 두 주 버전</li></ul></section></main>` + demoBar();
      return;
    }
    let screen = r.screen;
    if (!S.data && !S.loading) screen = 'open';
    if (S.loading && !S.data) screen = 'open';
    let main;
    switch (screen) {
      case 'open': main = screenOpen(); break;
      case 'overview': main = screenOverview(); break;
      case 'items': main = screenItems(r); break;
      case 'item': main = screenItem(r); break;
      case 'changes': main = screenChanges(); break;
      default: screen = 'progress'; main = screenProgress();
    }
    app.innerHTML = header(screen) + main + specPanel(screen) + demoBar();
    if (screen === 'items') renderItemTable();
    document.title = `kiotty-spec 뷰어 · ${({ open: '열기', overview: '개요', progress: '진행', items: '항목', item: r.arg, changes: '변경 요청' })[screen] || ''}`;
  }

  // ---------- events ----------
  window.addEventListener('hashchange', () => {
    const r = route();
    if (r.screen !== 'open') S.error = null;
    if (r.screen === 'item' || r.screen !== 'items') window.scrollTo(0, 0);
    render();
  });
  app.addEventListener('click', (e) => {
    const t = e.target;
    const spec = t.closest('[data-spec]');
    if (spec) { e.preventDefault(); S.specOpen = true; S.specTab = spec.dataset.spec; render(); return; }
    const act = t.closest('[data-act="spec"]');
    if (act) { S.specOpen = !S.specOpen; S.specTab = null; render(); return; }
    const copy = t.closest('[data-copy]');
    if (copy) { (navigator.clipboard ? navigator.clipboard.writeText(copy.dataset.copy) : Promise.reject()).then(() => toast('복사했습니다'), () => toast('복사하지 못했습니다', true)); return; }
    const more = t.closest('[data-more]');
    if (more) { const k = more.dataset.more; S.moreCols.has(k) ? S.moreCols.delete(k) : S.moreCols.add(k); render(); return; }
    if (t.closest('a')) return;
    const tog = t.closest('[data-toggle]');
    if (tog) { const id = tog.dataset.toggle; S.expanded.has(id) ? S.expanded.delete(id) : S.expanded.add(id); renderItemTable(); return; }
    const row = t.closest('[data-go]');
    if (row) go(row.dataset.go);
  });
  app.addEventListener('change', (e) => {
    const t = e.target; const r = route();
    if (t.dataset.act === 'demo') { S.demo = t.value; if (S.data || t.value !== 'nowasm') load(S.source || { repo: DUMMY.source.repo, ref: DUMMY.source.ref, path: DUMMY.source.path }, 200); else render(); return; }
    if (t.dataset.act === 'kind') { go(`#/items/${t.value}`); return; }
    if (t.dataset.act === 'cpstep') { S.cpStep = t.value; render(); return; }
    if (t.dataset.act === 'file') { openFiles(t.files); return; }
    if (t.dataset.tag !== undefined) {
      const tags = new Set(r.params.getAll('tag'));
      t.checked ? tags.add(t.dataset.tag) : tags.delete(t.dataset.tag);
      history.replaceState(null, '', itemsHash(r.arg, r.params.get('q') || '', tags));
      const det = t.closest('details');
      const name = t.dataset.tag.split(':')[0];
      const sel = Array.from(det.querySelectorAll('input:checked')).map((x) => x.dataset.tag.slice(name.length + 1));
      det.querySelector('summary').textContent = name + (sel.length ? ': ' + sel.join(', ') : '');
      det.classList.toggle('set', sel.length > 0);
      renderItemTable();
    }
  });
  app.addEventListener('input', (e) => {
    const t = e.target;
    if (t.dataset.act === 'q') { const r = route(); history.replaceState(null, '', itemsHash(r.arg, t.value, r.params.getAll('tag'))); renderItemTable(); }
  });
  app.addEventListener('submit', (e) => {
    e.preventDefault();
    const fd = new FormData(e.target);
    const repo = String(fd.get('repo') || '').trim(); const ref = String(fd.get('ref') || '').trim(); const p = String(fd.get('path') || '').trim();
    if (!/^[\w.-]+\/[\w.-]+$/.test(repo)) { document.getElementById('formerr').textContent = 'OWNER/NAME 꼴로 입력하세요'; return; }
    openRemote(repo, ref, p);
  });
  document.addEventListener('click', (e) => { for (const d of document.querySelectorAll('details.filter[open]')) if (!d.contains(e.target)) d.open = false; });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && S.specOpen) { S.specOpen = false; render(); } });
  // drop anywhere opens a file
  let dragDepth = 0;
  window.addEventListener('dragenter', (e) => { e.preventDefault(); dragDepth++; document.body.classList.add('dragging'); });
  window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; document.body.classList.remove('dragging'); } });
  window.addEventListener('dragover', (e) => e.preventDefault());
  window.addEventListener('drop', (e) => { e.preventDefault(); dragDepth = 0; document.body.classList.remove('dragging'); openFiles(e.dataTransfer.files); });

  // Start as if the link ?repo=acme/memo-sync&ref=v0.3.0 was opened.
  if (route().screen === 'open') render();
  else load({ repo: DUMMY.source.repo, ref: DUMMY.source.ref, path: DUMMY.source.path });
})();
