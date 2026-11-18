// 개요 화면 (UI-171) 의 그릴 값. overview 조회 JSON 을 받는 순수 함수다.

import { PURPOSE, ROLES, STEPS } from '../../i18n/ko.js';

export function buildOverview(answer, sourceName) {
  const kinds = answer.kinds.map((k) => ({ ...k, isZero: k.count === 0 }));
  return {
    title: answer.project.name || sourceName || '',
    createdAt: answer.project.created_at,
    itemCount: kinds.reduce((sum, k) => sum + k.count, 0),
    purpose: PURPOSE,
    roles: ROLES,
    steps: STEPS,
    kinds,
  };
}
