#!/usr/bin/env node
'use strict';
// Checks the development rules (spec RULE items) that can be counted mechanically.
// usage: node tools/lint.js [--rule 4.1] [--list] [--json]
// With no option it prints one line per rule: "<rule> <count>".
// --list prints every violation as "<rule> <file>:<line> <message>".

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const SCAN = ['include', 'src', 'app'];
const args = process.argv.slice(2);
const onlyRule = args.includes('--rule') ? args[args.indexOf('--rule') + 1] : null;
const listing = args.includes('--list') || onlyRule !== null;
const asJson = args.includes('--json');

const violations = [];
function report(rule, file, line, message) {
  violations.push({ rule, file, line, message });
}

function walk(directory, out) {
  if (!fs.existsSync(directory)) return out;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) walk(full, out);
    else if (/\.(h|hpp|cpp|c)$/.test(entry.name)) out.push(full);
  }
  return out;
}

const files = SCAN.flatMap((d) => walk(path.join(ROOT, d), [])).map((full) => {
  const rel = path.relative(ROOT, full).split(path.sep).join('/');
  const raw = fs.readFileSync(full, 'utf8').replace(/\r\n/g, '\n');
  return { rel, raw };
});

// ---------------------------------------------------------------- classification of paths
const isCore = (f) => f.startsWith('src/implementation/core/') || f.startsWith('app/cli/src/core/');
const isBackend = (f) => /\/backend\//.test(f);
const isAdapter = (f) => /^src\/kiotty_spec[^/]*\.(h|cpp)$/.test(f);
const isPublicHeader = (f) => f.startsWith('include/');
const isCli = (f) => f.startsWith('app/');
const isCliInfra = (f) => f.startsWith('app/cli/src/infra/');
const isCliI18n = (f) => f.startsWith('app/cli/src/i18n/');
const isLibrary = (f) => f.startsWith('src/');
const pointerExempt = (f) => isCore(f) || isBackend(f) || isAdapter(f) || isPublicHeader(f) || isCliInfra(f);
// docs/deviations.md: the CLI may allocate (1.1) with standard containers (1.6) whose sizes are not fixed (3.4);
// raw pointers are allowed where the CLI crosses the C ABI (1.10).
const allocationExempt = (f) => isCli(f);
const C_ABI_LINE = /\bKiottySpec_?[A-Z]\w*|\b(?:std::)?(?:fwrite|fread|fputs|fflush|fopen|fclose|strtoul|strtoull|strtol)\s*\(|\b(?:_dupenv_s|_wdupenv_s|free|MultiByteToWideChar|WideCharToMultiByte|GetCommandLineW|CommandLineToArgvW|LocalFree|SetConsoleOutputCP|GetStdHandle|GetConsoleMode|SetConsoleMode)\s*\(/;
const cAbiBoundary = (f, line) => isCli(f) && C_ABI_LINE.test(line);

// ---------------------------------------------------------------- lexing
// code: same length as raw, comments and literal contents blanked. Preprocessor lines blanked in `code`.
function lex(raw) {
  const out = raw.split('');
  const comments = new Set();
  const strings = []; // {line, text}
  let i = 0;
  let line = 1;
  const blank = (from, to) => {
    for (let k = from; k < to; ++k) if (out[k] !== '\n') out[k] = ' ';
  };
  while (i < raw.length) {
    const c = raw[i];
    const n = raw[i + 1];
    if (c === '\n') { line++; i++; continue; }
    if (c === '/' && n === '/') {
      const end = raw.indexOf('\n', i);
      const stop = end < 0 ? raw.length : end;
      comments.add(line);
      blank(i, stop);
      i = stop;
      continue;
    }
    if (c === '/' && n === '*') {
      let end = raw.indexOf('*/', i + 2);
      if (end < 0) end = raw.length - 2;
      for (let k = i; k < end + 2; ++k) if (raw[k] === '\n') { comments.add(line); line++; }
      comments.add(line);
      blank(i, end + 2);
      i = end + 2;
      continue;
    }
    if (c === 'R' && n === '"' && !/\w/.test(raw[i - 1] || '')) {
      const open = raw.indexOf('(', i);
      const delim = raw.slice(i + 2, open);
      const close = raw.indexOf(')' + delim + '"', open);
      const startLine = line;
      for (let k = i; k < close; ++k) if (raw[k] === '\n') line++;
      strings.push({ line: startLine, text: raw.slice(open + 1, close) });
      blank(open + 1, close);
      i = close + delim.length + 2;
      continue;
    }
    if (c === '"' || (c === '\'' && !/[0-9a-fA-F]/.test(raw[i - 1] || ''))) {
      let k = i + 1;
      while (k < raw.length && raw[k] !== c && raw[k] !== '\n') { if (raw[k] === '\\') k++; k++; }
      if (c === '"') strings.push({ line, text: raw.slice(i + 1, k) });
      blank(i + 1, k);
      i = k + 1;
      continue;
    }
    i++;
  }
  const code = out.join('');
  // preprocessor lines (with continuation) blanked into `body`
  const lines = code.split('\n');
  const pre = [];
  const bodyLines = lines.map((text, index) => {
    if (/^\s*#/.test(text) || (index > 0 && /\\\s*$/.test(lines[index - 1]) && /^\s*#/.test(pre[pre.length - 1] || ''))) {
      pre.push(text);
      return text.replace(/[^\n]/g, ' ');
    }
    return text;
  });
  return { code, body: bodyLines.join('\n'), comments, strings, codeLines: lines };
}

// ---------------------------------------------------------------- structure parsing
const CONTROL = /^(if|for|while|switch|catch)\b/;
const KEYWORDS = new Set(['if', 'for', 'while', 'switch', 'return', 'sizeof', 'alignof', 'catch', 'static_cast', 'const_cast',
  'reinterpret_cast', 'dynamic_cast', 'decltype', 'static_assert', 'alignas', 'noexcept', 'operator', 'throw', 'new', 'delete',
  'defined', 'case', 'else', 'do', 'typeid', 'explicit']);

function lineAt(starts, offset) {
  let lo = 0;
  let hi = starts.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (starts[mid] <= offset) lo = mid; else hi = mid - 1;
  }
  return lo + 1;
}

function matchParen(text, open) {
  let depth = 0;
  for (let k = open; k < text.length; ++k) {
    if (text[k] === '(') depth++;
    else if (text[k] === ')') { depth--; if (depth === 0) return k; }
  }
  return -1;
}

function splitTop(text) {
  const parts = [];
  let depth = 0;
  let angle = 0;
  let current = '';
  for (let k = 0; k < text.length; ++k) {
    const c = text[k];
    if ('([{'.includes(c)) depth++;
    else if (')]}'.includes(c)) depth--;
    else if (c === '<' && /\w/.test(text[k - 1] || '') && text[k + 1] !== '<' && text[k - 1] !== '<') angle++;
    else if (c === '>' && angle > 0 && text[k - 1] !== '-') angle--;
    if (c === ',' && depth === 0 && angle === 0) { parts.push(current); current = ''; continue; }
    current += c;
  }
  if (current.trim() !== '') parts.push(current);
  return parts.map((p) => p.trim()).filter((p) => p !== '');
}

// Finds the function-name paren of a header like "Result<A, B> Klass::name(int a) const : _x(a)".
function functionHead(header) {
  const text = header.replace(/\btemplate\s*<[^;{}]*?>\s*(?=[A-Za-z_])/g, (m) => m.replace(/[^\n]/g, ' '));
  for (let k = 0; k < text.length; ++k) {
    if (text[k] !== '(') continue;
    const before = text.slice(0, k);
    const name = /((?:~?[A-Za-z_]\w*(?:<[^()]*>)?::)*(?:operator\s*(?:[^\w\s(]+|\(\)|\w+)|~?[A-Za-z_]\w*))\s*$/.exec(before);
    if (!name) return null;
    const close = matchParen(text, k);
    if (close < 0) return null;
    const full = name[1].replace(/\s+/g, '');
    const bare = full.split('::').pop();
    if (KEYWORDS.has(bare)) return null;
    return { full, bare, params: text.slice(k + 1, close), prefix: before.slice(0, before.length - name[0].length), rest: text.slice(close + 1) };
  }
  return null;
}

function parseFile(file) {
  const lex_ = lex(file.raw);
  file.lex = lex_;
  const text = lex_.body;
  const starts = [0];
  for (let k = 0; k < text.length; ++k) if (text[k] === '\n') starts.push(k + 1);
  const L = (offset) => lineAt(starts, offset);
  const root = { type: 'file', children: [], statements: [], start: 0 };
  const stack = [root];
  let headerStart = 0;
  let paren = 0;
  file.functions = [];
  file.classes = [];
  file.blocks = [];
  file.enums = [];
  const top = () => stack[stack.length - 1];
  const inFunction = () => stack.some((f) => f.type === 'function');
  const enclosingFunction = () => [...stack].reverse().find((f) => f.type === 'function');
  const enclosingClass = () => [...stack].reverse().find((f) => f.type === 'class');

  function statement(endOffset) {
    const header = text.slice(headerStart, endOffset);
    const frame = top();
    if (header.trim() !== '') frame.statements.push({ text: header, start: headerStart, end: endOffset, line: L(headerStart + header.search(/\S|$/)) });
  }

  for (let k = 0; k < text.length; ++k) {
    const c = text[k];
    if (c === '(') paren++;
    else if (c === ')') paren--;
    if (paren > 0) continue;
    if (c === ';') {
      if (top().type === 'init') continue;
      statement(k);
      headerStart = k + 1;
      continue;
    }
    if (c === '{') {
      const header = text.slice(headerStart, k);
      const h = header.replace(/\b(public|private|protected)\s*:/g, ' ').trim();
      const parent = top();
      let type;
      if (parent.type === 'init' || parent.type === 'enum') type = 'init';
      else if (/\bnamespace\b/.test(h) || /^extern\s+"\s*"$/.test(h) || /^extern\s+""$/.test(h) || /^extern\s+"[^"]*"$/.test(h)) type = 'namespace';
      else if (/\benum\b/.test(h) && !/[=(]/.test(h.replace(/\benum\b.*$/, ''))) type = 'enum';
      else if (/(^|[\s>])(class|struct|union)\b[^=()]*$/.test(h) && !/\breturn\b/.test(h)) type = 'class';
      else if (parent.type === 'namespace' || parent.type === 'class' || parent.type === 'file') {
        const head = /\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(final\s*)?(->\s*[\w:<>&*,\s]+)?$/.test(h) || /\)\s*:[\s\S]*[)}]\s*$/.test(h) ? functionHead(h) : null;
        type = head ? 'function' : 'init';
      } else if (CONTROL.test(h) || /^else\s+if\b/.test(h)) type = 'control';
      else if (/^(else|do|try)$/.test(h)) type = 'block';
      else if (/\]\s*(\([^)]*\))?\s*(mutable\s*)?(->\s*[\w:<>&*]+\s*)?$/.test(h) && /\[[^\]]*\]/.test(h)) type = 'lambda';
      else if (/:$/.test(h) && /\b(case|default)\b/.test(h)) type = 'block';
      else if (h === '') type = 'block';
      else type = 'init';
      const frame = { type, header: h, rawHeader: header, open: k, openLine: L(k), headerLine: L(headerStart + header.search(/\S|$/)), children: [], statements: [], parent };
      if (type === 'class') {
        const m = /(class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)/.exec(h);
        frame.kind = m ? m[1] : 'struct';
        frame.name = m ? m[2] : '(anonymous)';
        frame.typedefName = null;
        frame.access = frame.kind === 'class' ? 'private' : 'public';
        frame.members = [];
        frame.methods = [];
        frame.sections = [];
        file.classes.push(frame);
      }
      if (type === 'enum') {
        const m = /enum\s+(?:class\s+|struct\s+)?(\w+)?\s*(?::\s*([\w:]+))?/.exec(h);
        frame.name = m && m[1] ? m[1] : '(anonymous)';
        frame.underlying = m && m[2] ? m[2] : null;
        frame.scoped = /enum\s+(class|struct)\b/.test(h);
        file.enums.push(frame);
      }
      if (type === 'function') {
        const head = functionHead(h);
        frame.name = head.full;
        frame.bare = head.bare;
        frame.params = splitTop(head.params).filter((p) => p !== 'void');
        frame.prefix = head.prefix;
        frame.rest = head.rest;
        frame.cls = enclosingClass();
        frame.anonymous = stack.some((f) => f.type === 'namespace' && /^namespace$/.test(f.header.trim()));
        frame.depth = 0;
        frame.maxDepth = 0;
        file.functions.push(frame);
        if (frame.cls) frame.cls.methods.push({ name: head.bare, access: frame.cls.access, line: frame.headerLine, params: frame.params, prefix: head.prefix, rest: head.rest, defined: true });
      }
      if (type === 'control' || type === 'block' || type === 'lambda') {
        const fn = enclosingFunction();
        if (fn) {
          const depth = stack.filter((f) => f.type === 'control' || f.type === 'block' || f.type === 'lambda').length + 1;
          fn.maxDepth = Math.max(fn.maxDepth, depth);
        }
        file.blocks.push(frame);
      }
      if (type !== 'init') {
        if (parent.type === 'class') applyAccess(parent, header);
        headerStart = k + 1;
      }
      parent.children.push(frame);
      stack.push(frame);
      continue;
    }
    if (c === '}') {
      const frame = stack.pop();
      if (!frame || stack.length === 0) { stack.push(root); continue; }
      frame.close = k;
      frame.closeLine = L(k);
      if (frame.type === 'init') continue;
      statement(k);
      if (frame.type === 'class' || frame.type === 'enum') {
        const after = /^\s*(\w+)?\s*;/.exec(text.slice(k + 1, k + 200));
        if (after && after[1]) frame.typedefName = after[1];
      }
      headerStart = k + 1;
      continue;
    }
  }
  file.L = L;
  file.text = text;
  for (const cls of file.classes) classMembers(file, cls);
}

function applyAccess(cls, header) {
  const all = [...header.matchAll(/\b(public|private|protected)\s*:/g)];
  if (all.length) {
    for (const m of all) cls.sections.push(m[1]);
    cls.access = all[all.length - 1][1];
  }
}

function classMembers(file, cls) {
  cls.access = cls.kind === 'class' ? 'private' : 'public';
  for (const st of cls.statements) {
    applyAccess(cls, st.text);
    const s = st.text.replace(/\b(public|private|protected)\s*:/g, ' ').trim();
    if (s === '' || /^(using|friend|typedef|static_assert|template\s*<[^>]*>\s*friend)\b/.test(s)) continue;
    const head = functionHead(s);
    const beforeParen = s.split('(')[0];
    if (head && !/[={]/.test(beforeParen)) {
      cls.methods.push({ name: head.bare, access: cls.access, line: st.line, params: splitTop(head.params).filter((p) => p !== 'void'), prefix: head.prefix, rest: head.rest, defined: false, text: s });
      continue;
    }
    const m = /^(.*?)([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*(\{[\s\S]*\}|=[\s\S]*)?$/.exec(s);
    if (!m) continue;
    const type = m[1].trim();
    if (type === '' || /^(return|case)\b/.test(type)) continue;
    cls.members.push({ name: m[2], type, array: m[3] || '', init: m[4] || '', access: cls.access, line: st.line, isStatic: /\bstatic\b/.test(type), text: s });
  }
}

files.forEach(parseFile);

// ---------------------------------------------------------------- size table (from static_assert(sizeof))
const sizes = new Map();
for (const f of files) {
  for (const m of f.lex.body.matchAll(/static_assert\s*\(\s*sizeof\s*\(\s*([\w:<>, ]+?)\s*\)\s*(==|<=)\s*(\d+)/g)) {
    sizes.set(m[1].replace(/\s+/g, '').split('::').pop(), Number(m[3]));
  }
}
const PRIMITIVE = { uint64_t: 8, int64_t: 8, size_t: 8, double: 8, uint32_t: 4, int32_t: 4, int: 4, float: 4, uint16_t: 2, int16_t: 2, uint8_t: 1, int8_t: 1, bool: 1, char: 1 };
const enumSizes = new Map();
for (const f of files) for (const e of f.enums) {
  const u = e.underlying ? PRIMITIVE[e.underlying.split('::').pop()] : 4;
  for (const n of [e.name, e.typedefName]) if (n) enumSizes.set(n, u || 4);
}
const aliases = new Map();
for (const f of files) {
  for (const m of f.lex.body.matchAll(/\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);/g)) aliases.set(m[1], m[2].trim());
}
const constants = new Map();
for (const f of files) {
  for (const m of f.lex.body.matchAll(/\bconstexpr\s+(?:size_t|uint\d+_t|int\d+_t|int|unsigned)\s+([A-Z_][A-Z0-9_]*)\s*=\s*([^;]+);/g)) constants.set(m[1], m[2]);
  for (const m of f.raw.matchAll(/^#define\s+([A-Z_][A-Z0-9_]*)\s+\(?(\d+)\)?\s*$/gm)) constants.set(m[1], m[2]);
}
function evaluate(expr, depth = 0) {
  if (depth > 8) return null;
  let failed = false;
  const replaced = expr.replace(/[A-Za-z_][\w:]*/g, (name) => {
    const bare = name.split('::').pop();
    if (!constants.has(bare)) { failed = true; return '0'; }
    const v = evaluate(constants.get(bare), depth + 1);
    if (v === null) failed = true;
    return String(v);
  }).replace(/(\d+)[uUlL]+/g, '$1');
  if (failed || !/^[\d\s+\-*/()]+$/.test(replaced)) return null;
  return Function(`return (${replaced});`)();
}
function sizeOf(type, depth = 0) {
  const name = type.replace(/\b(const|volatile|static|mutable|constexpr|inline)\b/g, '').trim().split('::').pop();
  if (depth < 5 && aliases.has(name) && !sizes.has(name)) return sizeOf(aliases.get(name), depth + 1);
  const t =type.replace(/\b(const|volatile|static|mutable|constexpr|inline|struct|enum|typename)\b/g, '').replace(/\s+/g, '').replace(/^.*::(?=[A-Za-z_]\w*(<|$))/, '');
  if (/[&*]$/.test(t)) return 8;
  if (PRIMITIVE[t] !== undefined) return PRIMITIVE[t];
  const fs_ = /^FixedString<(.+)>$/.exec(t);
  if (fs_) { const n = evaluate(fs_[1]); return n === null ? null : n + 1; }
  if (/^Span</.test(t)) return 16;
  if (sizes.has(t)) return sizes.get(t);
  if (enumSizes.has(t)) return enumSizes.get(t);
  return null;
}
function constantValue(expr) {
  return evaluate(expr);
}

// ---------------------------------------------------------------- per-file checks
const ABBREVIATIONS = new Set(['w', 't', 'res', 'idx', 'len', 'buf', 'ptr', 'tmp', 'cfg', 'str', 'num', 'cnt', 'ctx', 'msg', 'err', 'val', 'pos', 'src',
  'dst', 'arg', 'args', 'param', 'params', 'impl', 'util', 'utils', 'init', 'info', 'calc', 'prev', 'cur', 'curr', 'attr', 'db', 'fn', 'func', 'proc',
  'obj', 'elem', 'cb', 'desc', 'dir', 'doc', 'docs', 'env', 'expr', 'ext', 'hdr', 'lhs', 'rhs', 'mgr', 'ref', 'refs', 'req', 'resp', 'ret', 'sec', 'seq',
  'spec_', 'sz', 'vec', 'iter', 'it', 'opt', 'opts', 'tbl', 'col', 'cols', 'ch', 'ok']);
const VERBS = new Set(('acquire release open close fetch append parse read write create remove delete add set find search load save store insert ' +
  'update rename copy fill assign clear reset check validate is has can count compute build make format render print show list run start finish ' +
  'begin end resume confirm complete cancel apply merge diff compare collect gather convert split join cut trim encode decode hash sort push pop ' +
  'take put mark resolve refresh raise report emit flush seek scan walk visit match select filter advance move swap enter leave register lock ' +
  'unlock wait notify prune record restore rebuild attach detach bind link unlink sync emit publish subscribe allocate free grow shrink skip ' +
  'measure locate lookup query describe explain translate quote escape unescape trace log fail succeed recover reopen replace extend reserve ' +
  'drop keep pick choose order rank tally total sum need wrap unwrap name label tag untag group ungroup page fold unfold peek view expose ' +
  'export import serialize deserialize stamp compose combine flatten clone mirror prepare plan propose accept reject approve deny allow ' +
  'require ensure guard verify test probe sense detect reach return yield send receive transfer upload download issue assemble').split(/\s+/));
const CORE_ACCESSORS = new Set(['length', 'capacity', 'data', 'view', 'size', 'begin', 'end', 'empty', 'value', 'code', 'scratch', 'at', 'count']);

function words(name) {
  return name.replace(/^_+/, '').split(/_|(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])/).filter(Boolean).map((w) => w.toLowerCase());
}
function checkName(file, line, name, what) {
  if (/^(KiottySpec|KIOTTY_SPEC_)/.test(name)) return;
  const ws = words(name);
  for (const w of ws) if (ABBREVIATIONS.has(w)) report('4.5', file.rel, line, `${what} '${name}' uses abbreviation '${w}'`);
  if (ws.length > 4) report('4.5', file.rel, line, `${what} '${name}' has more than four words`);
}

const NON_TYPES = /^(return|break|continue|goto|delete|throw|case|using|typedef|else|do|co_return|namespace|template|friend|public|private|protected|static_assert)\b/;
function declarationOf(statementText) {
  const s = statementText.trim();
  if (s === '' || NON_TYPES.test(s) || /^[\w.]+\s*(\(|\[|->|\.|=|\+\+|--|[+\-*/%&|^]=|<<|>>)/.test(s.replace(/^\*+/, ''))) return null;
  const m = /^((?:const\s+|static\s+|constexpr\s+|inline\s+|thread_local\s+)*)([A-Za-z_][\w:]*(?:\s*<[\s\S]*?>)?(?:::\w+)*)\s*([&*]+\s*|\s+)((?:const\s+)?)([A-Za-z_]\w*|\[[\w\s,&]+\])\s*(\[[^\]]*\])?\s*([=({][\s\S]*)?$/.exec(s);
  if (!m) return null;
  const type = m[2];
  if (KEYWORDS.has(type) || /^(if|for|while|switch|return)$/.test(type)) return null;
  const init = m[7] || '';
  if (init.startsWith('(') && !/^\([^)]*\)\s*$/.test(init) && /[=]/.test(init) === false && /\)\s*$/.test(init) === false) return null;
  return { qualifiers: m[1], type, ref: m[3].trim(), name: m[5], array: m[6] || '', init, isConst: /\bconst\b|\bconstexpr\b/.test(m[1]) || (m[3].includes('*') && /\bconst\b/.test(m[4])) };
}

const resultFunctions = new Set();
for (const f of files) {
  for (const fn of f.functions) if (/\bResult\b|Result\s*$/.test(fn.prefix) || /Result\b/.test(fn.prefix)) resultFunctions.add(fn.bare);
  for (const c of f.classes) for (const m of c.methods) if (/Result\b/.test(m.prefix)) resultFunctions.add(m.name);
  for (const m of f.lex.body.matchAll(/(\w*Result\w*(?:<[^;{}()]*>)?)\s+([A-Za-z_]\w*)\s*\(/g)) resultFunctions.add(m[2]);
}

const classNames = new Set();
for (const f of files) for (const c of f.classes) classNames.add(c.name);

function checkFile(file) {
  const rel = file.rel;
  const rawLines = file.raw.split('\n');
  const bodyLines = file.lex.body.split('\n');

  // 4.1 line length, file length
  rawLines.forEach((t, i) => { if ([...t].length > 100) report('4.1', rel, i + 1, `line is ${[...t].length} characters`); });
  const templateHeader = isCore(rel) && /\.h$/.test(rel) && /\btemplate\s*</.test(file.lex.body);
  if (rawLines.length > 400 && !templateHeader) report('4.1', rel, rawLines.length, `file has ${rawLines.length} lines`);

  // 4.9 comments
  for (const line of file.lex.comments) report('4.9', rel, line, 'comment');

  // 4.14 macros
  rawLines.forEach((t, i) => {
    const m = /^\s*#\s*(\w+)\s*(.*)$/.exec(t);
    if (!m) return;
    const directive = m[1];
    if (isPublicHeader(rel)) return;
    if (directive === 'define' || directive === 'undef') report('4.14', rel, i + 1, `#${directive}`);
    if (/^(if|ifdef|ifndef|elif)$/.test(directive) && !/^!?\s*(defined\s*\(?\s*)?(_WIN32|_WIN64|__APPLE__|__linux__|__ANDROID__|__unix__)\b/.test(m[2])) {
      report('4.14', rel, i + 1, `#${directive} ${m[2]} is not a platform selection`);
    }
  });

  // 4.15 templates outside core
  if (!isCore(rel)) bodyLines.forEach((t, i) => { if (/\btemplate\s*</.test(t)) report('4.15', rel, i + 1, 'template outside core/'); });

  // 2.3 forbidden functions
  const FORBIDDEN = [
    [/\b(strcpy|strcat|strlen|strncpy|strncat|gets|sprintf|vsprintf|snprintf|vsnprintf)\s*\(/, 'C string function'],
    [/\b(atoi|atol|atoll|atof|strtol|strtoul|strtoll|strtoull|strtod|stoi|stol|stoul|stoull|stod)\s*\(/, 'unchecked number parse'],
    [/\b(memcpy|memset|memmove|memcmp)\s*\(/, 'direct memory function'],
    [/\b(malloc|calloc|realloc|free)\s*\(/, 'C allocation'],
    [/(^|[^\w.>:])new\s+[A-Za-z_(]/, 'raw new'],
    [/(^|[^\w.>:])delete\s+[\w(\[]/, 'raw delete'],
    [/\brand\s*\(/, 'rand'],
    [/\b(printf|fprintf|vprintf|vfprintf)\s*\(\s*[^"\s]/, 'printf with a non-constant format'],
  ];
  bodyLines.forEach((t, i) => {
    for (const [re, what] of FORBIDDEN) if (re.test(t)) report('2.3', rel, i + 1, what);
    if (/\breinterpret_cast\b/.test(t) && !isBackend(rel)) report('2.3', rel, i + 1, 'reinterpret_cast outside infra/*/backend');
    if (/\(\s*(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:u?int(?:8|16|32|64)_t|size_t|char|int|long|short|double|float|bool|void)\s*\**\s*\)\s*[\w(]/.test(t)) report('2.3', rel, i + 1, 'C-style cast');
  });

  // 1.1 / 1.6 containers and heap
  bodyLines.forEach((t, i) => {
    if (!allocationExempt(rel) && /\bstd::(string|wstring|map|unordered_map|set|unordered_set|multimap|list|deque|ostringstream|istringstream|stringstream|function|shared_ptr|any)\b(?!_view)/.test(t)) report('1.6', rel, i + 1, 'standard container other than std::vector');
    if (!allocationExempt(rel) && /\bstd::(to_string|getline)\b|\bstd::(cout|cerr|cin)\b|\bstd::(ifstream|ofstream|fstream)\b/.test(t)) report('1.1', rel, i + 1, 'allocating standard I/O or string helper');
  });
  for (const fn of file.functions) {
    const isInit = fn.bare === (fn.cls ? fn.cls.name : fn.name.split('::').slice(-2, -1)[0]) || /^create|^KiottySpec_create/.test(fn.bare) || fn.bare === 'main';
    const body = file.text.slice(fn.open, fn.close);
    if (isInit || allocationExempt(rel)) continue;
    const lines = body.split('\n');
    lines.forEach((t, j) => {
      const ln = fn.openLine + j;
      if (/\.(push_back|emplace_back|resize|reserve|insert|emplace|append)\s*\(/.test(t) && /\bstd::vector\b/.test(file.lex.body) ) report('1.1', rel, ln, 'container growth outside an initialization function');
      if (/\bstd::make_(unique|shared)\b|\bstd::vector\s*</.test(t) && !/\bstd::vector\s*<[^;=]*>\s*&/.test(t)) report('1.1', rel, ln, 'allocation outside an initialization function');
    });
  }

  // 1.10 raw pointers
  if (!pointerExempt(rel)) {
    bodyLines.forEach((t, i) => {
      if (cAbiBoundary(rel, t)) return;
      if (/\bnullptr\b/.test(t)) report('1.10', rel, i + 1, 'nullptr');
      if (/->/.test(t.replace(/\)\s*->\s*[\w:<>]+\s*$/, '').replace(/\]\s*(\([^)]*\))?\s*->/, ''))) report('1.10', rel, i + 1, 'pointer dereference ->');
      if (/\b[A-Za-z_][\w:]*(?:<[^<>]*>)?\s*\*\s*(const\s+)?[A-Za-z_]\w*\s*[,)=;{\[]/.test(t) && !/\w\s+\*\s+\w/.test(t.replace(/\b(const\s+)?[A-Z]\w*\s*\*\s*\w+\s*[,)=;]/, ''))) report('1.10', rel, i + 1, 'pointer type');
      else if (/\bconst\s+char\s*\*|\bchar\s*\*\s*\w/.test(t)) report('1.10', rel, i + 1, 'char pointer');
      if (/\.data\(\)/.test(t)) report('1.10', rel, i + 1, 'raw data() pointer');
    });
  }

  // 2.8 default in switch, enum if-chains
  bodyLines.forEach((t, i) => {
    if (/\bdefault\s*:/.test(t)) report('2.8', rel, i + 1, 'default label');
    if (/\belse\s+if\s*\(\s*[\w.]+\s*==\s*([A-Z][\w]*::)?[A-Z][A-Z0-9_]{2,}\b/.test(t)) report('2.8', rel, i + 1, 'enum dispatch with else-if');
  });

  // 5.5 / 8 visible sentences
  for (const s of file.lex.strings) {
    if (!/[가-힣]/.test(s.text)) continue;
    if (isLibrary(rel) && !/kiotty_model_catalog|\/entity\/kiotty_model/.test(rel)) report('5.5', rel, s.line, `library builds sentence "${s.text.slice(0, 30)}"`);
    if (isCli(rel) && !isCliI18n(rel)) report('8', rel, s.line, `sentence outside i18n/ "${s.text.slice(0, 30)}"`);
  }

  // 4.8 magic numbers
  const enumLines = new Set();
  for (const e of file.enums) for (let l = e.openLine; l <= (e.closeLine || e.openLine); ++l) enumLines.add(l);
  if (!isPublicHeader(rel)) {
    bodyLines.forEach((t, i) => {
      if (/\bconstexpr\b|\bstatic_assert\b|^\s*$/.test(t) || enumLines.has(i + 1)) return;
      const stripped = t.replace(/\b[A-Za-z_]\w*/g, ' ');
      for (const m of stripped.matchAll(/(?<![\w.])(0[xX][0-9a-fA-F']+|\d[\d']*(?:\.\d+)?)[uUlLfF]*\b/g)) {
        const v = m[1].replace(/'/g, '');
        if (v === '0' || v === '1' || v === '0.0' || v === '1.0') continue;
        report('4.8', rel, i + 1, `magic number ${m[1]}`);
      }
    });
  }

  // functions
  const definedNames = new Map();
  for (const fn of file.functions) definedNames.set(fn.bare, (definedNames.get(fn.bare) || []).concat([fn]));
  for (const fn of file.functions) {
    const bodyText = file.text.slice(fn.open + 1, fn.close);
    const bodyLength = fn.closeLine - fn.openLine - 1;
    if (bodyLength > 30) report('4.1', rel, fn.headerLine, `function '${fn.name}' body is ${bodyLength} lines`);
    if (fn.maxDepth > 2) report('4.1', rel, fn.headerLine, `function '${fn.name}' nests ${fn.maxDepth} levels`);
    if (fn.params.length > 4) report('4.1', rel, fn.headerLine, `function '${fn.name}' takes ${fn.params.length} arguments`);
    const isCtor = fn.cls ? fn.bare === fn.cls.name : fn.name.split('::').length > 1 && fn.name.split('::').slice(-2)[0] === fn.bare;
    const isDtor = fn.bare.startsWith('~');
    const isOperator = fn.bare.startsWith('operator');
    const isCApi = /^KiottySpec_/.test(fn.bare) || fn.bare === 'main';
    // 1.8 recursion
    if (!isCtor && !isOperator && new RegExp(`(^|[^\\w.>:])${fn.bare}\\s*\\(`).test(bodyText)) report('1.8', rel, fn.headerLine, `function '${fn.name}' calls itself`);
    // 4.5 / 4.18 function names
    if (!isCtor && !isDtor && !isOperator && !isCApi) {
      if (!/^[a-z][A-Za-z0-9]*$/.test(fn.bare)) report('4.5', rel, fn.headerLine, `function '${fn.bare}' is not camelCase`);
      checkName(file, fn.headerLine, fn.bare, 'function');
      const first = words(fn.bare)[0];
      const accessor = isCore(rel) && CORE_ACCESSORS.has(fn.bare);
      if (!accessor && (!VERBS.has(first) || /^(get|do|handle)[A-Z]/.test(fn.bare))) report('4.18', rel, fn.headerLine, `function '${fn.bare}' does not start with a verb`);
    }
    // 4.6 bool and output arguments, 5.4 Span<char> outputs, 3.6 passing
    if (!isCApi) for (const p of fn.params) checkParameter(file, fn, p, isCtor || isOperator);
    // statements inside
    checkBody(file, fn, bodyText);
  }
  // declarations in classes (not defined inline)
  for (const cls of file.classes) {
    for (const m of cls.methods) {
      if (m.defined) continue;
      const isCtor = m.name === cls.name || m.name.startsWith('~') || m.name.startsWith('operator');
      if (m.params.length > 4) report('4.1', rel, m.line, `method '${m.name}' takes ${m.params.length} arguments`);
      if (!isCtor) {
        if (!/^[a-z][A-Za-z0-9]*$/.test(m.name)) report('4.5', rel, m.line, `method '${m.name}' is not camelCase`);
        checkName(file, m.line, m.name, 'method');
        const first = words(m.name)[0];
        const accessor = isCore(rel) && CORE_ACCESSORS.has(m.name);
        if (!accessor && (!VERBS.has(first) || /^(get|do|handle)[A-Z]/.test(m.name))) report('4.18', rel, m.line, `method '${m.name}' does not start with a verb`);
      }
      for (const p of m.params) checkParameter(file, { name: m.name, headerLine: m.line, params: m.params }, p, isCtor);
    }
  }

  // 4.10 order: helper defined before its caller, private before public in a class
  for (const fn of file.functions) {
    const body = file.text.slice(fn.open + 1, fn.close);
    for (const [name, defs] of definedNames) {
      if (name === fn.bare || name.startsWith('operator') || name.startsWith('~')) continue;
      if (!new RegExp(`(^|[^\\w])${name}\\s*\\(`).test(body)) continue;
      for (const d of defs) if (d.headerLine < fn.headerLine && d.cls === fn.cls) { report('4.10', rel, d.headerLine, `'${name}' is defined above its caller '${fn.bare}'`); break; }
    }
  }
  for (const cls of file.classes) {
    const firstPrivate = cls.sections.indexOf('private');
    const firstPublic = cls.sections.indexOf('public');
    if (firstPrivate >= 0 && firstPublic > firstPrivate) report('4.10', rel, cls.openLine, `class '${cls.name}' has private before public`);
  }

  // classes: 4.1 public methods, 4.5 member names, 1.9 member init, 3.4 static_assert, 1.4 entities
  for (const cls of file.classes) {
    const publicMethods = new Set(cls.methods.filter((m) => m.access === 'public' && m.name !== cls.name && !m.name.startsWith('~') && !m.name.startsWith('operator') && !/=\s*(delete|default)/.test(m.rest || '')).map((m) => m.name));
    if (publicMethods.size > 7) report('4.1', rel, cls.openLine, `class '${cls.name}' has ${publicMethods.size} public methods`);
    if (publicMethods.size > 7) report('4.19', rel, cls.openLine, `class '${cls.name}' has ${publicMethods.size} public methods`);
    if (!/^[A-Z][A-Za-z0-9]*$/.test(cls.name) && !/^KiottySpec/.test(cls.name) && cls.name !== '(anonymous)') report('4.5', rel, cls.openLine, `type '${cls.name}' is not PascalCase`);
    checkName(file, cls.openLine, cls.name, 'type');
    for (const mem of cls.members) {
      if (mem.isStatic && /\bconstexpr\b/.test(mem.type)) continue;
      checkName(file, mem.line, mem.name, 'member');
      const isPrivate = mem.access !== 'public';
      if (isPrivate && !/^_[a-z][a-z0-9_]*$/.test(mem.name)) report('4.5', rel, mem.line, `private member '${mem.name}' is not _snake_case`);
      if (!isPrivate && !/^[a-z][a-z0-9_]*$/.test(mem.name) && !isPublicHeader(rel)) report('4.5', rel, mem.line, `public field '${mem.name}' is not snake_case`);
      if (/^bool$/.test(mem.type.replace(/\b(const|mutable)\b/g, '').trim()) && !/^_?(is|has|can)_/.test(mem.name)) report('4.5', rel, mem.line, `bool '${mem.name}' lacks is_/has_/can_`);
      if (mem.init === '' && !/&\s*$/.test(mem.type) && !isPublicHeader(rel) && !mem.isStatic && !initializedInConstructor(cls, mem.name)) report('1.9', rel, mem.line, `member '${mem.name}' has no initializer`);
      if (!pointerExempt(rel) && /\*\s*$/.test(mem.type)) report('1.10', rel, mem.line, `pointer member '${mem.name}'`);
    }
    if (cls.kind !== 'class' && cls.name !== '(anonymous)' && !allocationExempt(rel)) {
      const name = cls.typedefName && cls.name === '(anonymous)' ? cls.typedefName : cls.name;
      if (!sizes.has(name)) report('3.4', rel, cls.openLine, `struct '${name}' has no static_assert(sizeof)`);
      else checkOrder(file, cls);
      if (/\/entity\//.test(rel)) {
        const size = sizes.get(name);
        if (size !== undefined && size > 1024) report('1.4', rel, cls.openLine, `entity '${name}' is ${size} bytes`);
        if (cls.members.some((m) => /\*\s*$|\bSpan\s*</.test(m.type))) report('1.4', rel, cls.openLine, `entity '${name}' holds a pointer`);
      }
    }
  }
}

function initializedInConstructor(cls, name) {
  const re = new RegExp(`\\)\\s*:[\\s\\S]*\\b${name}\\s*[({]`);
  for (const f of files) for (const fn of f.functions) {
    const owner = fn.cls ? fn.cls.name : fn.name.split('::').slice(-2)[0];
    if (owner === cls.name && fn.bare === cls.name && re.test(fn.rawHeader)) return true;
  }
  return false;
}

function checkOrder(file, cls) {
  let previous = Infinity;
  for (const mem of cls.members) {
    if (mem.isStatic) continue;
    let size = sizeOf(mem.type);
    if (size === null) return;
    if (mem.array) {
      const n = constantValue(mem.array.replace(/[\[\]\s]/g, ''));
      if (n === null) return;
      size *= n;
    }
    if (size > previous) { report('3.4', file.rel, mem.line, `member '${mem.name}' (${size} B) follows a smaller member`); return; }
    previous = size;
  }
}

function checkParameter(file, fn, p, isCtorLike) {
  const rel = file.rel;
  const m = /^(.*?)([A-Za-z_]\w*)\s*(=.*)?$/.exec(p.replace(/\s+/g, ' ').trim());
  if (!m) return;
  const type = m[1].trim();
  const name = m[2];
  if (type === '') return;
  if (/^(const\s+)?bool$/.test(type)) report('4.6', rel, fn.headerLine, `bool argument '${name}' of '${fn.name}'`);
  if (/&$/.test(type) && !/\bconst\b/.test(type) && !/&&$/.test(type) && !isCtorLike) report('4.6', rel, fn.headerLine, `output argument '${name}' of '${fn.name}'`);
  if (/\bSpan\s*<\s*char\s*>/.test(type) && !/^(read|write|copy|fill|format|append)/.test(fn.name.split('::').pop()) && !isCore(rel) && !isAdapter(rel)) report('5.4', rel, fn.headerLine, `Span<char> output '${name}' of '${fn.name}'`);
  if (!/^[a-z][a-z0-9_]*$/.test(name)) report('4.5', rel, fn.headerLine, `argument '${name}' is not snake_case`);
  checkName(file, fn.headerLine, name, 'argument');
  const bare = type.replace(/\bconst\b/g, '').replace(/&$/, '').trim();
  const size = sizeOf(bare);
  if (size !== null) {
    if (/\bconst\b[^&]*&$/.test(type) && size <= 16) report('3.6', rel, fn.headerLine, `'${name}' (${size} B) should pass by value`);
    if (!/[&*]$/.test(type) && size > 16) report('3.6', rel, fn.headerLine, `'${name}' (${size} B) should pass by const&`);
  }
}

function conditionOf(header) {
  const m = /^(?:else\s+)?(if|while|switch|for)\s*(?:constexpr\s*)?\(/.exec(header);
  if (!m) return null;
  const open = header.indexOf('(', m[0].length - 1);
  const close = matchParen(header, open);
  let inner = header.slice(open + 1, close < 0 ? header.length : close);
  if (m[1] === 'for') {
    const parts = inner.split(';');
    if (parts.length < 3) return null;
    inner = parts[1];
  }
  return { keyword: m[1], text: inner.trim() };
}

function checkBody(file, fn, bodyText) {
  const rel = file.rel;
  const blocks = file.blocks.filter((b) => b.open > fn.open && b.close < fn.close);
  const loopVariables = [];
  // conditions, 4.3 / 4.4 / 4.12
  for (const b of blocks) {
    const cond = conditionOf(b.header);
    if (cond) {
      const t = cond.text;
      const call = /(^|[^\w])([A-Za-z_]\w*)\s*\(/.exec(t);
      if (call && !KEYWORDS.has(call[2])) report('4.4', rel, b.headerLine, `call '${call[2]}' inside a condition`);
      if (/[^=!<>]=[^=]/.test(t.replace(/->/g, ''))) report('4.4', rel, b.headerLine, 'assignment inside a condition');
      if (/!(?!=)/.test(t) && !/^!\s*[A-Za-z_]\w*$/.test(t)) report('4.12', rel, b.headerLine, `negated condition '${t.slice(0, 40)}'`);
      if (/!\s*!/.test(t)) report('4.12', rel, b.headerLine, 'double negation');
    }
    if (/^(else\s+)?if\b/.test(b.header)) {
      const stmts = b.statements.filter((s) => s.text.trim() !== '');
      if (stmts.length === 1 && /^\s*return\b/.test(stmts[0].text) && b.closeLine !== b.headerLine) report('4.3', rel, b.headerLine, 'error check is not a one-line block');
    }
    if (/^else\b/.test(b.header)) {
      const prev = b.parent.children[b.parent.children.indexOf(b) - 1];
      if (prev) {
        const last = prev.statements[prev.statements.length - 1];
        if (last && /^\s*return\b/.test(last.text)) report('4.3', rel, b.headerLine, 'else after return');
      }
    }
    if (/^for\s*\(/.test(b.header)) {
      const m = /^for\s*\(\s*(?:const\s+)?[\w:<>]+\s+([A-Za-z_]\w*)\s*=/.exec(b.header);
      if (m) loopVariables.push({ name: m[1], open: b.open, close: b.close });
    }
  }
  // statements
  const lines = bodyText.split('\n');
  const statements = [];
  const collect = (frame) => {
    for (const s of frame.statements) statements.push(s);
    for (const child of frame.children) if (child.type !== 'function') collect(child);
  };
  collect(fn);
  const locals = [];
  for (const s of statements) {
    const text = s.text.replace(/^\s*(case\s+[^:]+:|default\s*:)/, '').trim();
    // 4.13 nested ternary
    if ((text.match(/\?/g) || []).length > 1) report('4.13', rel, s.line, 'nested ternary');
    const d = declarationOf(text);
    if (d) {
      locals.push({ ...d, line: s.line, start: s.start });
      if (d.init === '' && !/&/.test(d.ref)) report('1.9', rel, s.line, `local '${d.name}' has no initializer`);
      if (/^\[/.test(d.name)) continue;
      if (!d.qualifiers.includes('constexpr') && !/^[a-z][a-z0-9_]*$/.test(d.name)) report('4.5', rel, s.line, `variable '${d.name}' is not snake_case`);
      if (d.qualifiers.includes('constexpr') && !/^[A-Z][A-Z0-9_]*$/.test(d.name)) report('4.5', rel, s.line, `constant '${d.name}' is not UPPER_SNAKE`);
      checkName(file, s.line, d.name, 'variable');
      if (/^bool$/.test(d.type) && !/^(is|has|can)_/.test(d.name)) report('4.5', rel, s.line, `bool '${d.name}' lacks is_/has_/can_`);
      if (d.type === 'auto') {
        const rhs = d.init.replace(/^[={]\s*/, '');
        const ctor = /^[A-Z]\w*(<[^;]*?>)?(::[A-Za-z_]\w*)*\s*[({]/.test(rhs);
        const lastCall = [...rhs.matchAll(/([A-Za-z_]\w*)\s*\(/g)].map((x) => x[1]).filter((x) => !KEYWORDS.has(x));
        const resultCall = lastCall.length > 0 && resultFunctions.has(lastCall[lastCall.length - 1]);
        if (!ctor && !resultCall) report('4.7', rel, s.line, `auto '${d.name}' without a visible type`);
      }
      // 1.7 stack buffers
      let size = sizeOf(d.type);
      if (d.array) {
        const n = constantValue(d.array.replace(/[\[\]\s]/g, ''));
        size = size !== null && n !== null ? size * n : size;
      }
      if (!d.ref.includes('&') && size !== null && size > 1024 && !d.qualifiers.includes('static')) report('1.7', rel, s.line, `local '${d.name}' is ${size} bytes on the stack`);
    } else {
      // 1.9 uninitialised declarations "Type name;"
      const m = /^((?:const\s+)?[A-Za-z_][\w:]*(?:<[^;]*>)?)\s+([a-z_]\w*)\s*(\[[^\]]*\])?$/.exec(text);
      if (m && !NON_TYPES.test(text) && !KEYWORDS.has(m[1])) report('1.9', rel, s.line, `local '${m[2]}' has no initializer`);
    }
    // 2.2 index operator outside a length loop
    if (!isCore(rel)) {
      for (const ix of text.matchAll(/([A-Za-z_][\w.]*|\))\s*\[\s*([^\]]+)\]/g)) {
        if (d && d.array && text.indexOf(ix[0]) === text.indexOf(d.name)) continue;
        const index = ix[2].trim();
        const covered = loopVariables.some((v) => v.name === index && s.start > v.open && s.start < v.close);
        if (!covered) report('2.2', rel, s.line, `operator[] with '${index}' outside a length-bounded loop`);
      }
    }
    // 2.4 unchecked multiplication
    if (/[\w)\]]\s+\*\s+[\w(]/.test(text) && !/\bconstexpr\b/.test(text)) report('2.4', rel, s.line, 'multiplication without checkedMultiply');
  }
  // 4.17 const by default
  for (const local of locals) {
    if (local.isConst || /^\[/.test(local.name) || local.init === '' || local.ref.includes('&')) continue;
    const after = file.text.slice(local.start, fn.close).slice(local.init.length + local.name.length);
    const n = local.name;
    const changed = new RegExp(`(^|[^\\w.])${n}\\s*(=[^=]|[+\\-*/%&|^]=|<<=|>>=|\\+\\+|--|\\.|\\[|->)|(\\+\\+|--|&)\\s*${n}\\b`).test(after);
    if (!changed) report('4.17', rel, local.line, `'${n}' never changes but is not const`);
  }
  // 4.11 member computation inside methods
  if (fn.cls || /::/.test(fn.name)) {
    for (const s of statements) {
      const t = s.text;
      if (/\b_[a-z]\w*/.test(t) && /(\b_[a-z]\w*(\.\w+\(\))?\s*([+\-*/%<>]|==|!=|<=|>=|&&|\|\|)\s*\w)|(\w\s*([+\-*/%<>]|==|!=|<=|>=|&&|\|\|)\s*\b_[a-z]\w*)/.test(t.replace(/->/g, '.').replace(/<<|>>/g, ' '))) {
        report('4.11', rel, s.line, 'method computes on members');
      }
    }
    for (const b of blocks) {
      const cond = conditionOf(b.header);
      if (cond && /\b_[a-z]\w*/.test(cond.text)) report('4.11', rel, b.headerLine, 'method branches on members');
    }
  }
}

for (const f of files) checkFile(f);

// ---------------------------------------------------------------- cross-file checks
// 4.16 callerless functions and getter/setter pairs
{
  const all = files.map((f) => ({ f, text: f.lex.body }));
  const counts = new Map();
  for (const { text } of all) for (const m of text.matchAll(/\b([A-Za-z_]\w*)\b/g)) counts.set(m[1], (counts.get(m[1]) || 0) + 1);
  const declared = new Map();
  const add = (name, f, line) => { if (!declared.has(name)) declared.set(name, []); declared.get(name).push({ f, line }); };
  for (const f of files) {
    for (const fn of f.functions) add(fn.bare, f, fn.headerLine);
    for (const c of f.classes) for (const m of c.methods) if (!m.defined) add(m.name, f, m.line);
  }
  for (const [name, places] of declared) {
    if (/^(KiottySpec_|main$|wmain$|operator|~)/.test(name) || classNames.has(name)) continue;
    const uses = (counts.get(name) || 0) - places.length;
    if (uses <= 0) report('4.16', places[0].f.rel, places[0].line, `'${name}' has no caller`);
  }
  for (const f of files) for (const c of f.classes) {
    const names = new Set(c.methods.map((m) => m.name));
    for (const n of names) if (names.has('set' + n[0].toUpperCase() + n.slice(1))) report('4.16', f.rel, c.openLine, `getter/setter pair '${n}' in '${c.name}'`);
  }
}

// 6.5 flows holding other flows of the same layer
{
  const usecaseClasses = new Set();
  for (const f of files) if (/\/domain\/usecases\//.test(f.rel)) for (const c of f.classes) if (c.kind === 'class') usecaseClasses.add(c.name);
  for (const f of files) if (/\/domain\/usecases\//.test(f.rel)) for (const c of f.classes) {
    for (const m of c.members) {
      const t = m.type.replace(/\b(const)\b/g, '').replace(/&/g, '').trim();
      if (usecaseClasses.has(t) && t !== c.name) report('6.5', f.rel, m.line, `'${c.name}' holds flow '${t}' of the same layer`);
    }
  }
}

// 6.2 global mutable state
for (const f of files) {
  const body = f.lex.body;
  for (const ns of [{ statements: [] }]) void ns;
  const walkFrames = (frame, inside) => {
    for (const s of frame.statements || []) {
      if (frame.type === 'namespace' || frame.type === 'file') {
        const d = declarationOf(s.text.trim());
        if (d && !d.isConst && !/^(using|typedef)/.test(s.text.trim()) && !/\(/.test(d.init.slice(0, 1)) && !/extern/.test(s.text)) report('6.2', f.rel, s.line, `global variable '${d.name}'`);
      }
      if (inside && /^\s*static\s+(?!constexpr|const\b)/.test(s.text)) report('6.2', f.rel, s.line, 'static local variable');
    }
    for (const child of frame.children || []) walkFrames(child, inside || child.type === 'function');
  };
  void body;
}

// 7.x directories and include direction
function layerOf(rel) {
  if (rel.startsWith('include/')) return 'interface';
  if (isAdapter(rel)) return 'adapter';
  const m = /^src\/implementation\/(context|core|domain\/entity|domain\/usecases|data\/repository|data\/states|data\/datasource|data\/infra)\//.exec(rel);
  if (m) return m[1];
  const c = /^app\/cli\/src\/(core|di|i18n|presentation|domain|datalayer|infra)\//.exec(rel);
  if (c) return 'cli/' + c[1];
  if (rel === 'app/cli/src/main.cpp') return 'cli/main';
  return null;
}
const ALLOWED = {
  core: ['core', 'interface'],
  'domain/entity': ['core', 'domain/entity', 'interface'],
  'data/infra': ['core', 'data/infra', 'interface'],
  'data/datasource': ['core', 'domain/entity', 'data/datasource', 'data/infra', 'interface'],
  'data/states': ['core', 'domain/entity', 'data/states', 'interface'],
  'data/repository': ['core', 'domain/entity', 'data/repository', 'data/states', 'data/datasource', 'data/infra', 'interface'],
  'domain/usecases': ['core', 'domain/entity', 'domain/usecases', 'data/repository', 'data/states', 'data/datasource', 'interface'],
  context: ['core', 'domain/entity', 'domain/usecases', 'data/repository', 'data/states', 'data/datasource', 'data/infra', 'context', 'interface'],
  adapter: ['core', 'domain/entity', 'domain/usecases', 'data/repository', 'data/states', 'data/datasource', 'context', 'adapter', 'interface'],
  interface: ['interface'],
  'cli/core': ['cli/core'],
  'cli/i18n': ['cli/core', 'cli/i18n'],
  'cli/infra': ['cli/core', 'cli/infra', 'interface'],
  'cli/domain': ['cli/core', 'cli/domain'],
  'cli/datalayer': ['cli/core', 'cli/domain', 'cli/datalayer', 'cli/infra', 'interface'],
  'cli/presentation': ['cli/core', 'cli/domain', 'cli/i18n', 'cli/infra', 'cli/presentation'],
  'cli/di': ['cli/core', 'cli/di', 'cli/domain', 'cli/datalayer', 'cli/i18n', 'cli/infra', 'cli/presentation', 'interface'],
  'cli/main': ['cli/core', 'cli/di', 'cli/domain', 'cli/datalayer', 'cli/i18n', 'cli/infra', 'cli/presentation', 'interface'],
};
for (const f of files) {
  const layer = layerOf(f.rel);
  if (layer === null) { report(isCli(f.rel) ? '7.2' : '7.1', f.rel, 1, 'file is outside the directory layout'); continue; }
  if (/^src\/implementation\/domain\/usecases\/[^/]+$/.test(f.rel)) report('7.1', f.rel, 1, 'usecase is not in a function folder');
  if (/^src\/implementation\/data\/repository\/[^/]+$/.test(f.rel)) report('7.1', f.rel, 1, 'repository is not in a module folder');
  if (isBackend(f.rel) && !/^src\/implementation\/data\/infra\/[^/]+\/backend\//.test(f.rel)) report('7.1', f.rel, 1, 'backend is not under data/infra/<class>/backend');
  f.raw.split('\n').forEach((t, i) => {
    const m = /^\s*#\s*include\s*"([^"]+)"/.exec(t);
    if (!m) return;
    let target = m[1];
    let targetRel = null;
    if (target === 'kiotty_spec.h') targetRel = 'include/kiotty_spec.h';
    else if (target.startsWith('implementation/')) targetRel = 'src/' + target;
    else targetRel = path.posix.normalize(path.posix.join(path.posix.dirname(f.rel), target));
    const targetLayer = layerOf(targetRel) || (fs.existsSync(path.join(ROOT, 'src', target)) ? layerOf('src/' + target) : null);
    if (targetLayer === null) return;
    const allowed = ALLOWED[layer] || [];
    if (!allowed.includes(targetLayer)) report('7.3', f.rel, i + 1, `${layer} includes ${targetLayer} (${target})`);
  });
}
// 7.4 core carries no domain words and no KiottySpec records
for (const f of files) {
  if (!isCore(f.rel)) continue;
  f.lex.body.split('\n').forEach((t, i) => {
    const w = /\b\w*(Item|Checkpoint|Relation|Milestone|Requirement|Prune|Kisp|Decision)\w*\b/.exec(t);
    if (w) report('7.4', f.rel, i + 1, `domain word '${w[0]}' in core/`);
    if (/\bKiottySpec(?!Status\b)[A-Z]\w*/.test(t)) report('7.4', f.rel, i + 1, 'KiottySpec record type in core/');
  });
}

// 0 deviations, 0.1 language standard, 2.9 build options
{
  const deviations = fs.existsSync(path.join(ROOT, 'docs/deviations.md')) ? fs.readFileSync(path.join(ROOT, 'docs/deviations.md'), 'utf8') : '';
  if (!/완화책/.test(deviations)) report('0', 'docs/deviations.md', 1, 'no 완화책 column');
  if (!/21\.6\.1/.test(deviations)) report('0', 'docs/deviations.md', 1, 'no MISRA 21.6.1 deviation');
  const cmake = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8');
  if (!/CMAKE_CXX_STANDARD\s+17/.test(cmake) || !/CMAKE_CXX_STANDARD_REQUIRED\s+ON/.test(cmake) || !/CMAKE_CXX_EXTENSIONS\s+OFF/.test(cmake)) report('0.1', 'CMakeLists.txt', 1, 'C++17 settings');
  for (const flag of ['-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wsign-compare', '-Wshadow', '-Wswitch', '-fno-exceptions', '-fno-rtti', '-fstack-protector-strong', '_FORTIFY_SOURCE=2', '/WX', 'sanitize=address', 'sanitize=undefined']) {
    if (!cmake.includes(flag)) report('2.9', 'CMakeLists.txt', 1, `missing ${flag}`);
  }
}

// 5.7 C interface memory: no union, no bool (KiottySpecBool instead)
for (const f of files) {
  if (!isPublicHeader(f.rel)) continue;
  f.lex.body.split('\n').forEach((t, i) => {
    if (/\bunion\b/.test(t)) report('5.7', f.rel, i + 1, 'union in the C interface');
    if (/\b(bool|_Bool)\b|<stdbool\.h>/.test(t)) report('5.7', f.rel, i + 1, 'bool in the C interface (use KiottySpecBool)');
  });
}

// 2.5 pointers handed out by the C interface
for (const f of files) {
  if (!isPublicHeader(f.rel)) continue;
  f.lex.body.split('\n').forEach((t, i) => {
    const m = /KIOTTY_SPEC_API\s+(const\s+)?(\w+)\s*\*/.exec(t);
    if (m && m[2] !== 'KiottySpecContext') report('2.5', f.rel, i + 1, `returns a ${m[2]} pointer`);
  });
  for (const c of f.classes) for (const mem of c.members) {
    // KiottySpecText and KiottySpecBuffer are input-only: the caller hands its own memory in, nothing is handed out.
    if (/\*/.test(mem.type) && !['KiottySpecText', 'KiottySpecBuffer'].includes(c.name) && !/KiottySpecContext/.test(mem.type)) report('2.5', f.rel, mem.line, `record '${c.name}' holds pointer '${mem.name}'`);
  }
}

// 5.1 sentinels
for (const f of files) {
  f.lex.body.split('\n').forEach((t, i) => {
    if (/\breturn\s+-\s*1\s*;/.test(t)) report('5.1', f.rel, i + 1, 'returns -1 as an error');
    if (/\berrno\b/.test(t) && !isBackend(f.rel)) report('5.1', f.rel, i + 1, 'errno outside a backend');
  });
}
// 5.3 large results returned by value
for (const f of files) for (const fn of f.functions) {
  const m = /Result<\s*([\w:<>]+)\s*,/.exec(fn.prefix) || /(\w+Result)\s*$/.exec(fn.prefix.trim());
  if (!m) continue;
  let inner = m[1];
  if (/Result$/.test(inner)) {
    const alias = new RegExp(`using\\s+${inner}\\s*=\\s*Result<\\s*([\\w:<>]+)\\s*,`).exec(files.map((x) => x.lex.body).join('\n'));
    if (!alias) continue;
    inner = alias[1];
  }
  if (/&$/.test(inner)) continue;
  const size = sizeOf(inner);
  if (size !== null && size > 1024) report('5.3', f.rel, fn.headerLine, `'${fn.bare}' returns ${inner} (${size} B) by value`);
}

// ---------------------------------------------------------------- 9 checklist (derived) and output
const CHECKLIST = { 1: ['1.1'], 2: [], 3: ['2.1'], 4: ['1.10', '2.2'], 5: ['2.3'], 6: ['2.4'], 7: ['3.4', '3.1'], 8: ['4.1'], 9: ['2.8'], 10: ['2.10'],
  11: ['5.2'], 12: ['4.9'], 13: ['4.18'], 14: ['7.4'], 15: ['5.6'] };
const byRule = new Map();
for (const v of violations) byRule.set(v.rule, (byRule.get(v.rule) || 0) + 1);
for (const [item, rules] of Object.entries(CHECKLIST)) {
  const n = rules.reduce((a, r) => a + (byRule.get(r) || 0), 0);
  if (n > 0) report('9', 'docs', 0, `checklist ${item} fails (${rules.join(', ')}: ${n})`);
}
byRule.set('9', violations.filter((v) => v.rule === '9').length);

const RULES = ['0', '0.1', '1.1', '1.4', '1.6', '1.7', '1.8', '1.9', '1.10', '2.2', '2.3', '2.4', '2.5', '2.8', '2.9', '3.4', '3.6', '4.1', '4.3', '4.4',
  '4.5', '4.6', '4.7', '4.8', '4.9', '4.10', '4.11', '4.12', '4.13', '4.14', '4.15', '4.16', '4.17', '4.18', '4.19', '5.1', '5.3', '5.4', '5.5', '5.7', '6.2',
  '6.5', '7.1', '7.2', '7.3', '7.4', '8', '9'];
const shown = violations.filter((v) => onlyRule === null || v.rule === onlyRule);
if (asJson) {
  process.stdout.write(JSON.stringify(shown, null, 1));
} else if (listing) {
  for (const v of shown) console.log(`${v.rule} ${v.file}:${v.line} ${v.message}`);
} else {
  for (const r of RULES) console.log(`${r} ${(byRule.get(r) || 0)}`);
  console.log(`total ${violations.length}`);
}
process.exitCode = violations.length > 0 ? 1 : 0;
