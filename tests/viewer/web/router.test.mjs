// UT-119 route parsing
import test from 'node:test';
import assert from 'node:assert/strict';
import { formatItemsRoute, parseRoute } from '../../../app/viewer/web/src/presentation/view/router.js';

test('route parsing', () => {
  assert.deepEqual(parseRoute('#/overview'), { screen: 'overview' });
  assert.deepEqual(parseRoute(''), { screen: 'progress' }, 'empty hash is progress');
  assert.deepEqual(parseRoute('#'), { screen: 'progress' });
  const items = parseRoute('#/items/requirement?tag=%EC%9A%94%EA%B5%AC%EC%82%AC%ED%95%AD%20%EC%A2%85%EB%A5%98:%ED%99%98%EA%B2%BD%20%EC%9A%94%EA%B5%AC%EC%82%AC%ED%95%AD&tag=a:b&q=%EB%B7%B0%EC%96%B4');
  assert.deepEqual(items, {
    screen: 'items', kind: 'requirement',
    tags: [{ name: '요구사항 종류', value: '환경 요구사항' }, { name: 'a', value: 'b' }],
    q: '뷰어',
  }, 'items route keeps kind, several tags and q decoded');
  assert.deepEqual(parseRoute('#/item/REQ-053'), { screen: 'item', id: 'REQ-053' }, 'item route gives id');
  assert.equal(parseRoute('#/nope').screen, 'unknown', 'unknown route is reported');
  const back = parseRoute(formatItemsRoute('design', { tags: [{ name: '구현 상태', value: '미구현' }], q: 'x y' }));
  assert.deepEqual(back, { screen: 'items', kind: 'design', tags: [{ name: '구현 상태', value: '미구현' }], q: 'x y' }, 'format and parse round trip');
});
