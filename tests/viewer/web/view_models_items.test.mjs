// UT-120 items filter, UT-121 change board
import test from 'node:test';
import assert from 'node:assert/strict';
import { buildItems, toggleTag } from '../../../app/viewer/web/src/presentation/viewModel/items.js';
import { buildBoard } from '../../../app/viewer/web/src/presentation/viewModel/changes.js';

const item = (id, title, tags = {}, parent = null, summary = '') => ({ id, kind: 'requirement', title, summary, checkpoint: 'CP-0001', parent, tags });
const answer = {
  kind: 'requirement',
  model: { name: '요구사항', tags: [{ name: '종류', values: ['기능', '품질', '환경'], cardinality: 'many' }] },
  items: [
    item('REQ-001', 'Viewer 열기', { 종류: ['기능'] }),
    item('REQ-002', '하위', { 종류: ['환경'] }, 'REQ-001'),
    item('REQ-003', '빌드', { 종류: ['환경', '품질'] }, null, '브라우저 VIEWER'),
  ],
};

test('items filter', () => {
  const all = buildItems(answer, { tags: [], q: '' }, []);
  assert.deepEqual(all.rows.map((r) => [r.item.id, r.depth]), [['REQ-001', 0], ['REQ-002', 1], ['REQ-003', 0]], 'child sits under its parent');
  const search = buildItems(answer, { tags: [], q: 'viewer' }, []);
  assert.deepEqual(search.rows.map((r) => r.item.id), ['REQ-001', 'REQ-003'], 'search ignores case on title and summary');
  const either = buildItems(answer, { tags: [{ name: '종류', value: '기능' }, { name: '종류', value: '품질' }], q: '' }, []);
  assert.deepEqual(either.rows.map((r) => r.item.id), ['REQ-001', 'REQ-003'], 'two values of one tag match either');
  const orphan = buildItems(answer, { tags: [{ name: '종류', value: '환경' }], q: '' }, []);
  assert.deepEqual(orphan.rows.map((r) => [r.item.id, r.depth]), [['REQ-002', 0], ['REQ-003', 0]], 'child stays when its parent is filtered out');
  assert.equal(`${orphan.count} / ${orphan.total}`, '2 / 3', 'result count is filtered over total');
  assert.equal(orphan.filters[0].selected[0], '환경');
  assert.equal(buildItems({ ...answer, items: [] }, { tags: [], q: '' }, []).isEmptyKind, true, 'empty kind gives its message');
  assert.equal(buildItems(answer, { tags: [], q: 'zzz' }, []).isNoMatch, true, 'no match gives its message');
  assert.deepEqual(toggleTag([{ name: 'a', value: '1' }], 'a', '1'), []);
  assert.deepEqual(toggleTag([], 'a', '1'), [{ name: 'a', value: '1' }]);
});

const cr = (n, state) => ({ id: `CR-${String(n).padStart(3, '0')}`, title: `t${n}`, summary: '', tags: state ? { 상태: [state], 출처: ['이슈'] } : {}, out_count: n });

test('change board', () => {
  const items = [cr(1, '완료'), cr(2, '완료'), cr(3, '완료'), cr(4, '완료'), cr(5, '완료'), cr(6, '완료'), cr(7, '완료'),
    cr(8, '열림'), cr(9, '반영 중'), cr(10, '백로그'), cr(11, '거부'), cr(12, null)];
  const board = buildBoard({ items });
  assert.deepEqual(board.columns.map((c) => c.key), ['backlog', 'active', 'done', 'rejected', 'none'], 'columns are backlog, open and reflecting, done, rejected, then no status');
  const done = board.columns[2];
  assert.deepEqual(done.cards.map((c) => c.id), ['CR-007', 'CR-006', 'CR-005', 'CR-004', 'CR-003'], 'cards are ID descending and five are shown before more');
  assert.equal(done.hidden, 2);
  assert.deepEqual(board.columns[1].cards.map((c) => c.states[0]), ['반영 중', '열림'], 'open column shows the state');
  assert.deepEqual(board.columns[0].cards[0].states, [], 'other columns hide the state');
  assert.equal(board.columns[1].cards[0].outCount, 9);
  const open = buildBoard({ items }, { expanded: new Set(['done']) });
  assert.equal(open.columns[2].cards.length, 7);
  assert.equal(open.columns[2].canFold, true);
  assert.equal(buildBoard({ items: [] }).isEmpty, true, 'no change requests gives its message');
  assert.equal(buildBoard({ items: [cr(1, '완료')] }).columns.length, 4, 'no extra column without unknown states');
});
