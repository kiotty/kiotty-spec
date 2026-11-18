// 본문 Markdown (DSN-124). parse 는 글을 블록 · 인라인 트리(평범한 객체)로, render 는 DOM 노드로 만든다.
// 글은 모두 텍스트 노드로 넣어 HTML · 스크립트는 글자 그대로 보인다 (UI-173).

const ITEM_ID = /\b[A-Z][A-Z0-9]*-\d{3,4}\b/;
const SAFE_URL = /^https?:\/\//i;
const BLOCK_START = /^(```|#{1,6}\s|\s*\||\s*([-*+]|\d+\.)\s|>\s?)/;
const LIST_ITEM = /^\s*([-*+]|\d+\.)\s+/;

// ---------- inline ----------
const INLINE_RULES = [
  { type: 'code', pattern: /`([^`]+)`/ },
  { type: 'link', pattern: /\[([^\]]+)\]\(([^)\s]+)\)/ },
  { type: 'strong', pattern: /\*\*([^*]+)\*\*/ },
  { type: 'em', pattern: /(?<![*\w])\*([^*\s][^*]*)\*(?!\w)/ },
  { type: 'item', pattern: ITEM_ID },
  { type: 'url', pattern: /https?:\/\/[^\s<>()]+[^\s<>().,;:'"]/ },
];

export function parseInline(text) {
  const out = [];
  let rest = String(text ?? '');
  while (rest) {
    let best = null;
    for (const rule of INLINE_RULES) {
      const m = rule.pattern.exec(rest);
      if (m && (!best || m.index < best.m.index)) best = { rule, m };
    }
    if (!best) { out.push({ type: 'text', text: rest }); break; }
    const { rule, m } = best;
    if (m.index > 0) out.push({ type: 'text', text: rest.slice(0, m.index) });
    if (rule.type === 'code') out.push({ type: 'code', text: m[1] });
    else if (rule.type === 'strong' || rule.type === 'em') out.push({ type: rule.type, children: parseInline(m[1]) });
    else if (rule.type === 'item') out.push({ type: 'item', id: m[0] });
    else if (rule.type === 'url') out.push({ type: 'link', href: m[0], children: [{ type: 'text', text: m[0] }] });
    else if (SAFE_URL.test(m[2])) out.push({ type: 'link', href: m[2], children: parseInline(m[1]) });
    else out.push({ type: 'text', text: m[0] });
    rest = rest.slice(m.index + m[0].length);
  }
  return out;
}

// ---------- block ----------
function splitCells(row) {
  return row.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map((c) => c.trim());
}

export function parse(text) {
  const lines = String(text ?? '').replace(/\r/g, '').split('\n');
  const blocks = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    const fence = /^```\s*(\S*)/.exec(line);
    if (fence) {
      const body = [];
      i += 1;
      while (i < lines.length && !/^```/.test(lines[i])) body.push(lines[i++]);
      i += 1;
      blocks.push({ type: 'code', lang: fence[1], text: body.join('\n') });
      continue;
    }
    const heading = /^(#{1,6})\s+(.*)$/.exec(line);
    if (heading) {
      blocks.push({ type: 'heading', level: heading[1].length, children: parseInline(heading[2]) });
      i += 1;
      continue;
    }
    if (/^\s*\|/.test(line)) {
      const rows = [];
      while (i < lines.length && /^\s*\|/.test(lines[i])) rows.push(lines[i++]);
      const kept = rows.filter((r, k) => !(k === 1 && /^[\s|:-]+$/.test(r)));
      const [head, ...body] = kept.map(splitCells);
      blocks.push({ type: 'table', head: head.map(parseInline), rows: body.map((r) => r.map(parseInline)) });
      continue;
    }
    if (LIST_ITEM.test(line)) {
      const ordered = /^\s*\d+\./.test(line);
      const items = [];
      const isSameList = (l) => LIST_ITEM.test(l) && (/^\s*\d+\./.test(l) === ordered || /^\s{2,}/.test(l));
      while (i < lines.length && isSameList(lines[i])) {
        items.push({ depth: Math.floor(/^\s*/.exec(lines[i])[0].length / 2), children: parseInline(lines[i].replace(LIST_ITEM, '')) });
        i += 1;
      }
      blocks.push({ type: 'list', ordered, items });
      continue;
    }
    if (/^>\s?/.test(line)) {
      const quote = [];
      while (i < lines.length && /^>\s?/.test(lines[i])) quote.push(lines[i++].replace(/^>\s?/, ''));
      blocks.push({ type: 'quote', children: parseInline(quote.join(' ')) });
      continue;
    }
    if (line.trim() === '') { i += 1; continue; }
    const para = [];
    while (i < lines.length && lines[i].trim() !== '' && !BLOCK_START.test(lines[i])) para.push(lines[i++]);
    if (para.length === 0) para.push(lines[i++]);
    blocks.push({ type: 'paragraph', children: parseInline(para.join('\n')) });
  }
  return blocks;
}

// ---------- render ----------
function appendInline(document, parent, nodes, options) {
  for (const node of nodes) {
    if (node.type === 'text') {
      const parts = node.text.split('\n');
      parts.forEach((part, k) => {
        if (k > 0) parent.appendChild(document.createElement('br'));
        parent.appendChild(document.createTextNode(part));
      });
    } else if (node.type === 'code') {
      const code = document.createElement('code');
      code.textContent = node.text;
      parent.appendChild(code);
    } else if (node.type === 'strong' || node.type === 'em') {
      const el = document.createElement(node.type);
      appendInline(document, el, node.children, options);
      parent.appendChild(el);
    } else if (node.type === 'link') {
      const a = document.createElement('a');
      a.href = node.href;
      a.rel = 'noopener noreferrer';
      a.target = '_blank';
      appendInline(document, a, node.children, options);
      parent.appendChild(a);
    } else if (node.type === 'item') {
      const href = options.itemHref?.(node.id);
      const el = document.createElement(href ? 'a' : 'span');
      if (href) el.href = href;
      el.className = 'iid';
      el.textContent = node.id;
      parent.appendChild(el);
    }
  }
}

function renderBlock(document, block, options) {
  const make = (tag, children) => {
    const el = document.createElement(tag);
    if (children) appendInline(document, el, children, options);
    return el;
  };
  switch (block.type) {
    case 'heading': return make(`h${Math.min(6, block.level + 2)}`, block.children);
    case 'paragraph': return make('p', block.children);
    case 'quote': return make('blockquote', block.children);
    case 'code': {
      const pre = document.createElement('pre');
      if (block.lang) pre.dataset.lang = block.lang;
      pre.textContent = block.text;
      return pre;
    }
    case 'list': {
      const list = document.createElement(block.ordered ? 'ol' : 'ul');
      for (const item of block.items) {
        const li = make('li', item.children);
        if (item.depth > 0) li.className = `depth-${Math.min(item.depth, 3)}`;
        list.appendChild(li);
      }
      return list;
    }
    case 'table': {
      const wrap = document.createElement('div');
      wrap.className = 'scroll';
      const table = document.createElement('table');
      table.className = 'md';
      const head = document.createElement('thead');
      const headRow = document.createElement('tr');
      for (const cell of block.head) headRow.appendChild(make('th', cell));
      head.appendChild(headRow);
      const body = document.createElement('tbody');
      for (const row of block.rows) {
        const tr = document.createElement('tr');
        for (const cell of row) tr.appendChild(make('td', cell));
        body.appendChild(tr);
      }
      table.append(head, body);
      wrap.appendChild(table);
      return wrap;
    }
    default: return document.createTextNode('');
  }
}

// options.itemHref(id): 항목 ID 를 링크로 만들 주소. 없으면 링크로 만들지 않는다.
export function render(tree, document, options = {}) {
  const fragment = document.createDocumentFragment();
  for (const block of tree) fragment.appendChild(renderBlock(document, block, options));
  return fragment;
}
