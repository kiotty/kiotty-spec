#!/usr/bin/env node
// Builds docs/viewer_prototype.html: a clickable, responsive prototype of the
// browser viewer (CR-019) for feedback before Step 4.
//
//   node tools/ui_prototype/build.js [--root DIR] [--out FILE] [--ks PATH]
//
// What comes from ks (so the prototype follows the spec):
//   - the GUI view items UI-168 and below (titles, bodies; shown in the "명세" panel)
//   - the Kind list and each Kind's tags and relations (ks model)
//   - the overview text and the 12 steps (ks guide) and the short step names (ks next)
// Only the project data (items, checkpoints, ks next result) is dummy: dummy.json.
// The real viewer reads that data from a .kisp file through the wasm32 library.
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const here = __dirname;
const repoRoot = path.resolve(here, '..', '..');
const root = path.resolve(opt('--root', repoRoot));
const out = path.resolve(opt('--out', path.join(repoRoot, 'docs', 'viewer_prototype.html')));
const ksPath = opt('--ks', process.env.KS || 'ks');

const ks = (...a) => execFileSync(ksPath, [...a, '--root', root], { encoding: 'utf8', maxBuffer: 64 << 20 }).replace(/\r/g, '');
function tsv(text) {
  const lines = text.split('\n').filter((l) => l.length > 0);
  const head = lines.shift().split('\t');
  return lines.map((l) => { const c = l.split('\t'); const o = {}; head.forEach((h, i) => { o[h] = c[i] ?? ''; }); return o; });
}

// GUI view items
const gui = tsv(ks('item', 'list', '--kind', 'external_interface', '--tag', '인터페이스 종류=GUI 뷰'))
  .map((v) => ({ id: v.id, title: v.title, summary: v.summary, parent: v.parent, body: ks('item', 'body', v.id) }));

// Models
const kinds = tsv(ks('model')).map((k) => {
  const defs = tsv(ks('model', k.kind));
  return {
    kind: k.kind, name: k.name, prefix: k.prefix, step: k.step,
    tags: defs.filter((d) => d.type === 'tag').map((d) => ({ name: d.name, values: d.values.split('|'), many: d.cardinality === 'many' })),
    relations: defs.filter((d) => d.type === 'relation').map((d) => ({ name: d.name, to: d.values, many: d.cardinality === 'many' })),
  };
});

// Guide: [목적] [역할] [12 단계] sections
const guideText = ks('guide');
const section = (name) => {
  const m = guideText.match(new RegExp('\\[' + name + '\\]\\n([\\s\\S]*?)(?=\\n\\[|$)'));
  return m ? m[1].replace(/\n+$/, '') : '';
};
const steps = section('12 단계').split('\n')
  .map((l) => l.match(/^\s*(\d+)\s+(\S.*?)\s{2,}(\S.*?)\s{2,}(\S.*)$/))
  .filter(Boolean)
  .map((m) => ({ step: Number(m[1]), name: m[2], does: m[3], kinds: m[4] }));
const title = guideText.split('\n')[0];
const shortSteps = (ks('next').split('\n')[1] || '').split(' → ').map((s) => s.replace(/\s*\(V\)\s*$/, '').trim());

const spec = { title, purpose: section('목적'), roles: section('역할'), steps, shortSteps, kinds, gui };
const dummy = JSON.parse(fs.readFileSync(path.join(here, 'dummy.json'), 'utf8'));
const json = (o) => JSON.stringify(o).replace(/</g, '\\u003c');

const html = `<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>kiotty-spec 뷰어 (프로토타입)</title>
<!-- 생성 파일: node tools/ui_prototype/build.js. 화면 명세 · 모델 · 안내 글은 ks 에서, 프로젝트 데이터는 dummy.json 에서 온다. 직접 고치지 않는다. -->
<style>
${fs.readFileSync(path.join(here, 'app.css'), 'utf8')}
</style>
</head>
<body>
<div id="app"></div>
<script type="application/json" id="spec">${json(spec)}</script>
<script type="application/json" id="dummy">${json(dummy)}</script>
<script>
${fs.readFileSync(path.join(here, 'app.js'), 'utf8')}
</script>
</body>
</html>
`;
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, html, 'utf8');
console.log(`${path.relative(repoRoot, out)}: ${gui.length} views, ${kinds.length} kinds, ${dummy.items.length} dummy items`);
