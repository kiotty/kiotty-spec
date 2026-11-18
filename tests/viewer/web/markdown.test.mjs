// UT-122 markdown is safe
import test from 'node:test';
import assert from 'node:assert/strict';
import { parse, parseInline } from '../../../app/viewer/web/src/core/markdown.js';

const BODY = [
  '## 진술',
  '첫 줄 <script>alert(1)</script>',
  '<img src=x onerror=alert(1)>',
  '',
  '- 하나 REQ-053',
  '  - 둘',
  '1. 첫째',
  '',
  '| 이름 | 값 |',
  '| --- | --- |',
  '| a | `b` |',
  '',
  '```wireframe',
  '화면 <b>x</b>',
  '```',
  '> 인용',
  '[문서](https://example.com/a) [나쁜](javascript:alert(1)) https://kiotty.dev/x.',
].join('\n');

function texts(nodes) {
  return nodes.flatMap((n) => (n.type === 'text' ? [n.text] : n.children ? texts(n.children) : n.text ? [n.text] : []));
}

test('markdown is safe', () => {
  const tree = parse(BODY);
  assert.deepEqual(tree.map((b) => b.type), ['heading', 'paragraph', 'list', 'list', 'table', 'code', 'quote', 'paragraph'],
    'headings, lists, table, code block and quote become nodes');
  assert.equal(tree[0].level, 2);
  const para = texts(tree[1].children).join('');
  assert.ok(para.includes('<script>alert(1)</script>') && para.includes('<img src=x onerror=alert(1)>'), 'HTML tags stay text nodes');
  assert.ok(tree[1].children.every((n) => n.type === 'text'), 'no element comes from HTML');
  assert.deepEqual(tree[2].items.map((i) => i.depth), [0, 1], 'nested list keeps depth');
  assert.ok(tree[2].items[0].children.some((n) => n.type === 'item' && n.id === 'REQ-053'), 'item IDs become links');
  assert.equal(tree[3].ordered, true);
  assert.deepEqual(tree[4].rows[0][1], [{ type: 'code', text: 'b' }]);
  assert.equal(tree[5].lang, 'wireframe');
  assert.equal(tree[5].text, '화면 <b>x</b>', 'code block keeps text as is');
  const links = tree[7].children.filter((n) => n.type === 'link');
  assert.deepEqual(links.map((l) => l.href), ['https://example.com/a', 'https://kiotty.dev/x'], 'http and https become links, trailing dot is left out');
  assert.ok(texts(tree[7].children).join('').includes('[나쁜](javascript:alert(1))'), 'javascript: link stays text');
  assert.deepEqual(parseInline('**굵게** *기울임*').map((n) => n.type), ['strong', 'text', 'em']);
});

// 아주 작은 가짜 DOM: 요소는 tag · 속성 · 자식만 가진다.
function fakeDocument() {
  const element = (tag) => ({
    tag, children: [], dataset: {}, className: '', href: undefined,
    appendChild(child) { this.children.push(child); return child; },
    append(...nodes) { this.children.push(...nodes); },
    set textContent(text) { this.children = [{ tag: '#text', text }]; },
  });
  return {
    createElement: element,
    createTextNode: (text) => ({ tag: '#text', text }),
    createDocumentFragment: () => element('#fragment'),
  };
}

function walk(node, out = []) {
  out.push(node);
  for (const child of node.children ?? []) walk(child, out);
  return out;
}

test('markdown renders text only', async () => {
  const { render } = await import('../../../app/viewer/web/src/core/markdown.js');
  const nodes = walk(render(parse(BODY), fakeDocument(), { itemHref: (id) => `#/item/${id}` }));
  const tags = new Set(nodes.map((n) => n.tag));
  for (const bad of ['script', 'img', 'b']) assert.ok(!tags.has(bad), `no <${bad}> element`);
  assert.ok(nodes.some((n) => n.tag === '#text' && n.text.includes('<script>alert(1)</script>')), 'script stays a text node');
  const anchors = nodes.filter((n) => n.tag === 'a');
  assert.deepEqual(anchors.map((a) => a.href), ['#/item/REQ-053', 'https://example.com/a', 'https://kiotty.dev/x'], 'only safe links become anchors');
  assert.ok(['h4', 'ul', 'ol', 'table', 'pre', 'blockquote'].every((t) => tags.has(t)));
});

// 공통 수칙 2.10 의 다섯 입력: 빈 입력, 아주 긴 입력, NUL, 잘못된 UTF-8 (짝 없는 대리 문자).
test('markdown parsing takes the five inputs', () => {
  assert.deepEqual(parse(''), [], 'empty');
  const long = parse(`${'가'.repeat(100000)}\n`);
  assert.equal(long.length, 1, 'long line is one paragraph');
  assert.equal(long[0].children[0].text.length, 100000);
  assert.equal(parse('a\0b')[0].children[0].text, 'a\0b', 'NUL stays text');
  assert.equal(parse('x \uD800 y')[0].children[0].text, 'x \uD800 y', 'lone surrogate stays text');
  assert.deepEqual(parseInline(''), []);
});
