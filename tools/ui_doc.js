#!/usr/bin/env node
// Builds docs/viewer_interface.html from the GUI view items (external_interface,
// 인터페이스 종류=GUI 뷰) in the spec. The page is generated only from ks output,
// so change the items with ks and run this again; do not edit the HTML.
//
//   node tools/ui_doc.js [--root DIR] [--out FILE] [--ks PATH]
//
// Item bodies are Markdown. A fenced block with the language "wireframe" is drawn
// as a screen sketch. One element per line, two spaces of indent per level:
//   화면 ADDRESS     browser window          가로 [LABEL]  horizontal group
//   세로 LABEL       titled box              머리 TEXT     viewer name
//   탭 A | *B        tabs (* = selected)     진행 A | *B   step line
//   표 H1 | H2       table, child 행 A | B   입력 HINT     text box
//   고르기 TEXT      select box              버튼 TEXT     button
//   링크 [*]TEXT     link                    배지 TEXT     badge
//   제목 TEXT        bold title              글 TEXT       plain text
//   상자 TEXT        drop zone               알림 TEXT     error banner
//   본문 TEXT        expanded body preview
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const repoRoot = path.resolve(__dirname, '..');
const root = path.resolve(opt('--root', repoRoot));
const out = path.resolve(opt('--out', path.join(repoRoot, 'docs', 'viewer_interface.html')));
const ksPath = opt('--ks', process.env.KS || 'ks');
const GUI_TAG = '인터페이스 종류=GUI 뷰';

function ks(...a) {
  return execFileSync(ksPath, [...a, '--root', root], { encoding: 'utf8', maxBuffer: 64 << 20 }).replace(/\r/g, '');
}
function tsv(text) {
  const lines = text.split('\n').filter((l) => l.length > 0);
  const head = lines.shift().split('\t');
  return lines.map((l) => { const c = l.split('\t'); const o = {}; head.forEach((h, i) => { o[h] = c[i] ?? ''; }); return o; });
}
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

// ---- data --------------------------------------------------------------
const views = tsv(ks('item', 'list', '--kind', 'external_interface', '--tag', GUI_TAG));
const viewIds = new Set(views.map((v) => v.id));
const reqCache = new Map();
function requirement(id) {
  if (!reqCache.has(id)) reqCache.set(id, tsv(ks('item', 'show', id))[0] || { id, title: '' });
  return reqCache.get(id);
}
for (const v of views) {
  v.body = ks('item', 'body', v.id);
  v.reqs = tsv(ks('relation', 'list', v.id)).filter((r) => r.from === v.id && r.relation === '관련 요구사항').map((r) => requirement(r.to));
}
const checkpoint = tsv(ks('checkpoint', 'show'))[0] || {};
const cpLine = checkpoint.checkpoint ? `${checkpoint.checkpoint} · ${checkpoint.step} · ${checkpoint.state}` : '';

// ---- inline / markdown ---------------------------------------------------
function inline(s) {
  let h = esc(s);
  h = h.replace(/`([^`]+)`/g, '<code>$1</code>');
  h = h.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  h = h.replace(/\b(UI-\d{3})\b/g, (m) => (viewIds.has(m) ? `<a href="#${m}">${m}</a>` : m));
  return h;
}
function markdown(md) {
  const lines = md.split('\n');
  const html = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    const fence = line.match(/^```\s*(\S*)/);
    if (fence) {
      const buf = [];
      i++;
      while (i < lines.length && !/^```/.test(lines[i])) buf.push(lines[i++]);
      i++;
      html.push(fence[1] === 'wireframe' ? wireframe(buf) : `<pre>${esc(buf.join('\n'))}</pre>`);
      continue;
    }
    const h = line.match(/^(#{2,4})\s+(.*)/);
    if (h) { html.push(`<h${h[1].length + 1}>${inline(h[2])}</h${h[1].length + 1}>`); i++; continue; }
    if (/^\s*\|/.test(line)) {
      const rows = [];
      while (i < lines.length && /^\s*\|/.test(lines[i])) rows.push(lines[i++]);
      const cells = (r) => r.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
      const body = rows.filter((r, k) => !(k === 1 && /^[\s|:-]+$/.test(r)));
      const [head, ...rest] = body;
      html.push('<table class="md"><thead><tr>' + cells(head).map((c) => `<th>${inline(c)}</th>`).join('') + '</tr></thead><tbody>' +
        rest.map((r) => '<tr>' + cells(r).map((c) => `<td>${inline(c)}</td>`).join('') + '</tr>').join('') + '</tbody></table>');
      continue;
    }
    if (/^\s*([-*]|\d+\.)\s+/.test(line)) {
      const ordered = /^\s*\d+\./.test(line);
      const items = [];
      while (i < lines.length && /^\s*([-*]|\d+\.)\s+/.test(lines[i])) items.push(lines[i++].replace(/^\s*([-*]|\d+\.)\s+/, ''));
      const tag = ordered ? 'ol' : 'ul';
      html.push(`<${tag}>` + items.map((t) => `<li>${inline(t)}</li>`).join('') + `</${tag}>`);
      continue;
    }
    if (line.trim() === '') { i++; continue; }
    const para = [];
    while (i < lines.length && lines[i].trim() !== '' && !/^(```|#{2,4}\s|\s*\||\s*([-*]|\d+\.)\s)/.test(lines[i])) para.push(lines[i++]);
    html.push(`<p>${inline(para.join(' '))}</p>`);
  }
  return html.join('\n');
}

// ---- wireframe -----------------------------------------------------------
function wireframe(lines) {
  const rootNode = { kind: 'root', text: '', children: [] };
  const stack = [{ depth: -1, node: rootNode }];
  for (const raw of lines) {
    if (raw.trim() === '') continue;
    const depth = Math.floor(raw.match(/^ */)[0].length / 2);
    const t = raw.trim();
    const sp = t.indexOf(' ');
    const node = { kind: sp < 0 ? t : t.slice(0, sp), text: sp < 0 ? '' : t.slice(sp + 1), children: [] };
    while (stack[stack.length - 1].depth >= depth) stack.pop();
    stack[stack.length - 1].node.children.push(node);
    stack.push({ depth, node });
  }
  return `<div class="wf">${rootNode.children.map(wfNode).join('')}</div>`;
}
const parts = (s) => s.split('|').map((p) => p.trim());
const pick = (p) => (p.startsWith('*') ? `<span class="on">${esc(p.slice(1).trim())}</span>` : `<span>${esc(p)}</span>`);
function wfNode(n) {
  const kids = () => n.children.map(wfNode).join('');
  switch (n.kind) {
    case '화면': return `<div class="win"><div class="bar"><i></i><i></i><i></i><span class="addr">${esc(n.text)}</span></div><div class="page">${kids()}</div></div>`;
    case '가로': return `<div class="row">${n.text ? `<div class="cap">${esc(n.text)}</div>` : ''}${kids()}</div>`;
    case '세로': return `<fieldset class="col"><legend>${esc(n.text)}</legend>${kids()}</fieldset>`;
    case '머리': return `<span class="brand">${esc(n.text)}</span>${kids()}`;
    case '탭': return `<div class="tabs">${parts(n.text).map(pick).join('')}</div>`;
    case '진행': return `<div class="steps">${parts(n.text).map(pick).join('')}</div>`;
    case '표': return `<table class="wft"><thead><tr>${parts(n.text).map((c) => `<th>${esc(c)}</th>`).join('')}</tr></thead><tbody>` +
      n.children.map((r) => `<tr>${parts(r.text).map((c) => `<td>${esc(c)}</td>`).join('')}</tr>`).join('') + '</tbody></table>';
    case '입력': return `<span class="input">${esc(n.text)}</span>`;
    case '고르기': return `<span class="select">${esc(n.text)} ▾</span>`;
    case '버튼': return `<span class="button">${esc(n.text)}</span>`;
    case '링크': return n.text.startsWith('*') ? `<span class="link on">${esc(n.text.slice(1))}</span>` : `<span class="link">${esc(n.text)}</span>`;
    case '배지': return `<span class="badge">${esc(n.text)}</span>`;
    case '제목': return `<strong class="title">${esc(n.text)}</strong>`;
    case '글': return `<div class="text">${esc(n.text)}</div>`;
    case '상자': return `<div class="drop">${esc(n.text)}</div>`;
    case '알림': return `<div class="alert">${esc(n.text)}</div>`;
    case '본문': return `<div class="preview">${esc(n.text)}</div>`;
    default: return `<div class="text">${esc((n.kind + ' ' + n.text).trim())}</div>${kids()}`;
  }
}

// ---- page ----------------------------------------------------------------
const order = [];
for (const v of views.filter((x) => x.parent === '-' || !viewIds.has(x.parent))) {
  order.push(v);
  for (const c of views.filter((x) => x.parent === v.id)) order.push(c);
}
const nav = order.map((v) => `<li class="${v.parent !== '-' ? 'sub' : ''}"><a href="#${v.id}">${v.id} ${esc(v.title)}</a></li>`).join('');
const sections = order.map((v) => `
<section id="${v.id}">
  <div class="ihead"><span class="id">${v.id}</span><h2>${esc(v.title)}</h2>${v.parent !== '-' ? `<span class="parent">상위 <a href="#${v.parent}">${v.parent}</a></span>` : ''}</div>
  ${v.summary && v.summary !== '-' ? `<p class="summary">${inline(v.summary)}</p>` : ''}
  ${v.reqs.length ? `<p class="reqs">관련 요구사항 ${v.reqs.map((r) => `<span class="req" title="${esc(r.summary && r.summary !== '-' ? r.summary : '')}">${r.id} ${esc(r.title)}</span>`).join(' ')}</p>` : ''}
  ${markdown(v.body)}
</section>`).join('\n');

const html = `<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>브라우저 뷰어 외부 인터페이스</title>
<!-- 생성 파일: node tools/ui_doc.js 가 ks 의 GUI 뷰 항목에서 만든다. 직접 고치지 않는다. -->
<style>
:root { --fg:#1d232b; --muted:#5b6673; --line:#d6dbe1; --bg:#f6f7f9; --accent:#2f6fde; --accent-bg:#e7efff; --warn:#b3261e; --warn-bg:#fdecea; }
* { box-sizing: border-box; }
body { margin:0; font:15px/1.6 -apple-system, "Segoe UI", "Malgun Gothic", "Apple SD Gothic Neo", sans-serif; color:var(--fg); background:#fff; display:flex; }
nav { position:sticky; top:0; height:100vh; overflow:auto; width:250px; flex:none; padding:20px 16px; background:var(--bg); border-right:1px solid var(--line); font-size:14px; }
nav h1 { font-size:16px; margin:0 0 4px; } nav .meta { color:var(--muted); font-size:12px; margin-bottom:14px; }
nav ul { list-style:none; margin:0; padding:0; } nav li { margin:3px 0; } nav li.sub { padding-left:14px; }
nav a { color:var(--fg); text-decoration:none; } nav a:hover { color:var(--accent); }
main { flex:1; min-width:0; padding:24px 40px 80px; max-width:1100px; }
section { border-top:1px solid var(--line); padding-top:18px; margin-top:28px; }
.ihead { display:flex; align-items:baseline; gap:10px; flex-wrap:wrap; }
.ihead h2 { margin:0; font-size:22px; } .id { font:600 13px ui-monospace, Consolas, monospace; color:var(--accent); background:var(--accent-bg); padding:2px 6px; border-radius:4px; }
.parent, .summary { color:var(--muted); } .summary { margin:6px 0; }
.reqs { font-size:13px; } .req { display:inline-block; border:1px solid var(--line); border-radius:12px; padding:1px 9px; margin:2px 2px; background:#fff; }
h3 { font-size:16px; margin:22px 0 6px; } h4 { font-size:15px; }
code, pre { font-family: ui-monospace, Consolas, "D2Coding", monospace; font-size:13px; } code { background:var(--bg); padding:1px 4px; border-radius:3px; }
pre { background:var(--bg); border:1px solid var(--line); padding:10px 12px; overflow:auto; }
table.md { border-collapse:collapse; margin:8px 0; font-size:14px; } table.md th, table.md td { border:1px solid var(--line); padding:4px 10px; text-align:left; vertical-align:top; } table.md th { background:var(--bg); }
.wf { margin:10px 0 6px; }
.win { border:1px solid #b9c1cb; border-radius:8px; overflow:hidden; box-shadow:0 2px 10px rgba(0,0,0,.07); font-size:13px; }
.bar { display:flex; align-items:center; gap:6px; background:#e9ecf0; padding:7px 10px; border-bottom:1px solid #c9d0d8; }
.bar i { width:10px; height:10px; border-radius:50%; background:#c4cad2; display:inline-block; }
.addr { margin-left:8px; flex:1; background:#fff; border:1px solid #cfd5dc; border-radius:12px; padding:1px 10px; color:var(--muted); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; font-family:ui-monospace, Consolas, monospace; font-size:12px; }
.page { padding:12px; display:flex; flex-direction:column; gap:8px; background:#fff; }
.row { display:flex; align-items:flex-start; gap:10px; flex-wrap:wrap; } .row > .col { flex:1; min-width:180px; } .cap { width:100%; color:var(--muted); font-size:12px; }
.col { border:1px dashed #b9c1cb; border-radius:6px; margin:0; padding:6px 10px 10px; display:flex; flex-direction:column; gap:6px; align-items:flex-start; }
.col legend { font-weight:600; font-size:12px; color:var(--muted); padding:0 4px; }
.brand { font-weight:700; }
.tabs { display:flex; gap:2px; border-bottom:2px solid var(--line); width:100%; } .tabs span { padding:4px 12px; color:var(--muted); } .tabs span.on { color:var(--accent); border-bottom:2px solid var(--accent); margin-bottom:-2px; font-weight:600; }
.steps { display:flex; flex-wrap:wrap; gap:4px; } .steps span { border:1px solid var(--line); border-radius:10px; padding:0 8px; font-size:12px; color:var(--muted); } .steps span.on { background:var(--accent); border-color:var(--accent); color:#fff; font-weight:600; }
table.wft { border-collapse:collapse; width:100%; } table.wft th, table.wft td { border-bottom:1px solid var(--line); padding:3px 8px; text-align:left; } table.wft th { background:var(--bg); font-weight:600; }
.input, .select { display:inline-block; min-width:180px; border:1px solid #b9c1cb; border-radius:4px; padding:3px 8px; color:#8a94a0; background:#fff; }
.button { display:inline-block; border-radius:4px; padding:3px 14px; background:var(--accent); color:#fff; font-weight:600; }
.link { color:var(--accent); text-decoration:underline; } .link.on { font-weight:700; text-decoration:none; background:var(--accent-bg); padding:0 4px; border-radius:3px; }
.badge { display:inline-block; border-radius:10px; padding:0 8px; background:var(--bg); border:1px solid var(--line); font-size:12px; }
.title { font-size:15px; } .text { color:var(--fg); }
.drop { width:100%; border:2px dashed #9fb4d8; border-radius:8px; padding:22px; text-align:center; color:var(--muted); background:#f8faff; }
.alert { width:100%; border-left:4px solid var(--warn); background:var(--warn-bg); color:var(--warn); padding:6px 10px; border-radius:4px; }
.preview { width:100%; background:var(--bg); border-left:3px solid var(--line); padding:6px 10px; color:var(--muted); }
@media (max-width: 800px) { body { display:block; } nav { position:static; width:auto; height:auto; } main { padding:16px; } }
</style>
</head>
<body>
<nav>
  <h1>브라우저 뷰어 외부 인터페이스</h1>
  <div class="meta">ks 의 GUI 뷰 항목에서 생성 · ${esc(cpLine)}<br><a href="viewer_prototype.html">눌러 보는 프로토타입 (더미 데이터) →</a></div>
  <ul>${nav}</ul>
</nav>
<main>
<p class="summary">이 페이지는 <code>node tools/ui_doc.js</code> 가 ks 의 외부 인터페이스 항목(인터페이스 종류 = GUI 뷰) ${views.length} 개에서 만든다. 고칠 때는 ks 로 항목을 고치고 다시 만든다. 화면 그림은 항목 본문의 <code>wireframe</code> 블록이다.</p>
${sections}
</main>
</body>
</html>
`;
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, html,'utf8');
console.log(`${path.relative(repoRoot, out)}: ${views.length} views`);
