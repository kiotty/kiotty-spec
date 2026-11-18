// 변경 요청 보드 (UI-175) 와 항목 상세 (UI-174) 의 그릴 값. 조회 JSON 을 받는 순수 함수다.

import { BOARD_COLUMNS } from '../../i18n/ko.js';

export const CARD_LIMIT = 5;
const STATUS_TAG = '상태';
export const COLUMNS = [
  { key: 'backlog', title: BOARD_COLUMNS.backlog, values: ['백로그'] },
  { key: 'active', title: BOARD_COLUMNS.active, values: ['열림', '반영 중'], showState: true },
  { key: 'done', title: BOARD_COLUMNS.done, values: ['완료'] },
  { key: 'rejected', title: BOARD_COLUMNS.rejected, values: ['거부'] },
];

function idNumber(id) {
  const m = /-(\d+)$/.exec(id);
  return m ? Number(m[1]) : 0;
}

function toCard(item, showState) {
  return {
    id: item.id,
    title: item.title,
    summary: item.summary,
    states: showState ? (item.tags[STATUS_TAG] ?? []) : [],
    sources: item.tags['출처'] ?? [],
    entries: item.tags['진입 단계'] ?? [],
    outCount: item.out_count ?? 0,
  };
}

export function buildBoard(answer, { expanded = new Set() } = {}) {
  const items = [...answer.items].sort((a, b) => idNumber(b.id) - idNumber(a.id));
  if (items.length === 0) return { isEmpty: true, columns: [] };
  const known = COLUMNS.flatMap((c) => c.values);
  const columns = COLUMNS.map((c) => ({ ...c, list: items.filter((x) => (x.tags[STATUS_TAG] ?? []).some((v) => c.values.includes(v))) }));
  const none = items.filter((x) => !(x.tags[STATUS_TAG] ?? []).some((v) => known.includes(v)));
  if (none.length) columns.push({ key: 'none', title: BOARD_COLUMNS.none, values: [], showState: true, list: none });
  return {
    isEmpty: false,
    columns: columns.map((c) => {
      const isOpen = expanded.has(c.key);
      const shown = isOpen ? c.list : c.list.slice(0, CARD_LIMIT);
      return {
        key: c.key,
        title: c.title,
        count: c.list.length,
        cards: shown.map((x) => toCard(x, c.showState)),
        hidden: c.list.length - shown.length,
        canFold: isOpen && c.list.length > CARD_LIMIT,
      };
    }),
  };
}

function groupRelations(rows) {
  const groups = new Map();
  for (const row of rows) {
    if (!groups.has(row.relation)) groups.set(row.relation, []);
    groups.get(row.relation).push(row);
  }
  return [...groups.entries()].map(([name, list]) => ({ name, list }));
}

export function buildItem(answer) {
  return {
    item: answer.item,
    tags: Object.entries(answer.tags).map(([name, values]) => ({ name, values })),
    body: answer.body,
    out: groupRelations(answer.out),
    in: groupRelations(answer.in),
    impact: answer.impact,
    children: answer.children,
    isChangeRequest: answer.item.kind === 'change_request',
  };
}
