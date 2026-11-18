// UT-121 change board and progress text (V2: 진행 부분), 개요 viewModel
import test from 'node:test';
import assert from 'node:assert/strict';
import { buildProgress } from '../../../app/viewer/web/src/presentation/viewModel/progress.js';
import { buildOverview } from '../../../app/viewer/web/src/presentation/viewModel/overview.js';

const checkpoints = [
  { checkpoint: 'CP-0002', step: 2, state: 'running', description: '요구사항', created_at: 2, previous: 'CP-0001' },
  { checkpoint: 'CP-0001', step: 1, state: 'done', description: '접수', created_at: 1, previous: null },
];

test('progress text', () => {
  const running = buildProgress({ position: { checkpoint: 'CP-0002', step: 2, state: 'running', description: '요구사항' }, unmet: [], checkpoints });
  assert.equal(running.status, '사용자 확정을 기다립니다', 'running with no unmet rows says waiting for user');
  assert.equal(running.current.stepName, '요구사항');
  assert.deepEqual(running.current.chips.slice(0, 3).map((c) => c.mark), ['past', 'on', ''], 'chips mark past and current steps');
  assert.equal(running.checkpoints.length, 2);

  const blocked = buildProgress({ position: { checkpoint: 'CP-0002', step: 2, state: 'running', description: '' }, unmet: [{ id: 'REQ-001', kind: 'requirement', title: 't', condition: 'ACCEPTANCE_CRITERIA' }], checkpoints });
  assert.equal(blocked.status, '', 'unmet rows leave no status text');
  assert.equal(blocked.unmet[0].reason, '인수 조건 없음', 'condition becomes its reason');

  const done = buildProgress({ position: { checkpoint: 'CP-0002', step: 2, state: 'done', description: '' }, unmet: [], checkpoints });
  assert.equal(done.status, '다음 단계를 기다립니다', 'done says waiting for next step');

  assert.equal(buildProgress({ position: null, unmet: [], checkpoints: [] }).isEmpty, true, 'no position says no progress record');
  assert.deepEqual(buildProgress({ position: { checkpoint: 'CP-0002', step: 2, state: 'running', description: '' }, unmet: [], checkpoints }, { stepFilter: 1 }).checkpoints.map((c) => c.checkpoint), ['CP-0001'], 'step filter');
});

test('overview counts', () => {
  const view = buildOverview({ project: { name: null, created_at: 5 }, kinds: [{ kind: 'requirement', name: '요구사항', prefix: 'REQ', count: 2 }, { kind: 'milestone', name: '마일스톤', prefix: 'MS', count: 0 }] }, 'o/n');
  assert.equal(view.title, 'o/n', 'missing project name uses the source');
  assert.equal(view.itemCount, 2);
  assert.deepEqual(view.kinds.map((k) => k.isZero), [false, true]);
  assert.equal(view.steps.length, 12);
});
