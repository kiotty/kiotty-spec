// 진행 화면 (UI-172) 의 그릴 값. progress 조회 JSON 을 받는 순수 함수다.

import { CONDITION_REASONS, STATE_NAMES, STEPS, TEXT } from '../../i18n/ko.js';

export function stepName(step) {
  return STEPS.find((s) => s.step === step)?.name ?? String(step);
}

export function describeStatus(position, unmet) {
  if (!position) return '';
  if (position.state === 'done') return TEXT.waitingNext;
  return unmet.length === 0 ? TEXT.waitingUser : '';
}

export function buildProgress(answer, { stepFilter = 0 } = {}) {
  const position = answer.position;
  if (!position) return { isEmpty: true };
  const current = {
    checkpoint: position.checkpoint,
    step: position.step,
    stepName: stepName(position.step),
    state: position.state,
    stateName: STATE_NAMES[position.state] ?? position.state,
    description: position.description,
    chips: STEPS.map((s) => ({
      step: s.step,
      name: s.short,
      mark: s.step === position.step ? 'on' : s.step < position.step ? 'past' : '',
    })),
  };
  const unmet = answer.unmet.map((row) => ({
    id: row.id,
    kind: row.kind,
    title: row.title,
    reason: CONDITION_REASONS[row.condition] ?? row.condition,
  }));
  const checkpoints = answer.checkpoints
    .filter((row) => !stepFilter || row.step === stepFilter)
    .map((row) => ({
      ...row,
      stepName: stepName(row.step),
      stateName: STATE_NAMES[row.state] ?? row.state,
    }));
  return {
    isEmpty: false,
    current,
    unmet,
    status: describeStatus(position, answer.unmet),
    checkpoints,
  };
}
