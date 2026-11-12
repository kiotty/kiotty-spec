// Record a code inspection into the code_inspection_result items.
// Lint-based items are re-run with tools/lint.js; manual items come from a results file
// ("=== <rule>" blocks with 결과 / 근거 / 판단 / 위반 위치); section items are summed from their rules.
// node tools/record_inspection.js <manual results file> [--dry]   (run inside a Step 10 checkpoint)
const fs = require('fs');
const { execFileSync, spawnSync } = require('child_process');
const KS = 'build/Debug/ks.exe';
const ks = (...args) => execFileSync(KS, args, { encoding: 'utf8' }).replace(/\r/g, '');
const DRY = process.argv.includes('--dry');
const manualFile = process.argv[2];

const manual = {};
for (const block of fs.readFileSync(manualFile, 'utf8').replace(/\r\n/g, '\n').split(/^=== /m).slice(1)) {
  const newline = block.indexOf('\n');
  manual[block.slice(0, newline).trim()] = block.slice(newline + 1).trim() + '\n';
}

const rows = ks('item', 'list', '--kind', 'code_inspection_result').split('\n').slice(1).filter(Boolean).map((l) => l.split('\t'));
const items = rows.map(([id, , title, summary]) => ({ id, title, summary, rule: title.split(' ')[0].replace(/\.$/, ''), body: ks('item', 'body', id) }));
const results = {};

function lintRule(rule) {
  const run = spawnSync('node', ['tools/lint.js', '--rule', rule, '--list'], { encoding: 'utf8' });
  return run.stdout.replace(/\r\n/g, '\n').split('\n').filter((line) => line.startsWith(rule + ' '))
    .map((line) => line.slice(rule.length + 1)).sort();
}
const countOf = (body) => { const m = /^결과: FAIL \((\d+)건\)/m.exec(body); return m ? Number(m[1]) : (/^결과: FAIL/m.test(body) ? -1 : 0); };

// leaf rules first, then sections
for (const item of items) {
  if (/^근거: 하위 수칙/m.test(item.body)) continue;
  let body;
  if (/^근거: node tools\/lint\.js/m.test(item.body)) {
    const found = lintRule(item.rule);
    const note = (/^판단: .*$/m.exec(item.body) || [''])[0];
    const judged = note ? note + '\n' : '';
    body = found.length === 0
      ? `결과: PASS\n근거: node tools/lint.js --rule ${item.rule}\n${judged}`
      : `결과: FAIL (${found.length}건)\n근거: node tools/lint.js --rule ${item.rule}\n${judged}위반 위치:\n${found.join('\n')}\n`;
  } else {
    body = manual[item.rule];
    if (!body) throw new Error('no manual result for ' + item.rule);
    const listed = (body.split('위반 위치:\n')[1] || '').split('\n').filter((l) => l.trim()).length;
    if (countOf(body) > 0 && countOf(body) !== listed) throw new Error(`${item.rule}: count ${countOf(body)} != listed ${listed}`);
  }
  results[item.rule] = body;
}
for (const item of items) {
  if (!/^근거: 하위 수칙/m.test(item.body)) continue;
  const children = Object.keys(results).filter((rule) => rule.startsWith(item.rule + '.'));
  const failed = children.filter((rule) => /^결과: FAIL/m.test(results[rule]))
    .sort((a, b) => a.split('.').map(Number)[1] - b.split('.').map(Number)[1]);
  results[item.rule] = `결과: ${failed.length ? 'FAIL' : 'PASS'}\n근거: 하위 수칙 결과의 합\n판단: 하위 수칙 ${children.length}개 중 FAIL: ${failed.length ? failed.join(', ') : '없음'}\n`;
}

const changes = [];
for (const item of items) {
  const body = results[item.rule];
  const count = countOf(body);
  const summary = count === 0 ? 'PASS' : count > 0 ? `FAIL ${count}건` : 'FAIL';
  const verdict = count === 0 ? 'PASS' : 'FAIL';
  if (body.trimEnd() !== item.body.trimEnd() || summary !== item.summary) {
    changes.push(`${item.id} ${item.rule}: ${item.summary} -> ${summary}`);
    if (!DRY) {
      fs.writeFileSync(require('path').join(require('os').tmpdir(), 'kiotty_cir_body.md'), body);
      ks('item', 'set', item.id, '--body-file', require('path').join(require('os').tmpdir(), 'kiotty_cir_body.md'), '--summary', summary);
      ks('tag', 'set', item.id, '결과', verdict);
    }
  }
}
console.log(changes.join('\n'));
console.log(`${changes.length} changed`);
