// 항목 표 화면 (UI-173) 의 그릴 값. items 조회 JSON 과 # 매개변수를 받는 순수 함수다.

function hasText(item, q) {
  if (!q) return true;
  const needle = q.toLowerCase();
  return item.title.toLowerCase().includes(needle) || item.summary.toLowerCase().includes(needle);
}

// 같은 태그의 값 여럿은 그중 하나라도, 다른 태그끼리는 모두 맞아야 한다.
function groupTags(tags) {
  const groups = new Map();
  for (const { name, value } of tags) {
    if (!groups.has(name)) groups.set(name, []);
    groups.get(name).push(value);
  }
  return groups;
}

function matchesTags(item, groups) {
  for (const [name, values] of groups) {
    const have = item.tags[name] ?? [];
    if (!have.some((v) => values.includes(v))) return false;
  }
  return true;
}

// 하위 항목은 상위 바로 아래에 들여 쓴다. 상위가 걸러져 빠지면 하위는 제자리에 남는다.
export function orderRows(items) {
  const ids = new Set(items.map((x) => x.id));
  const children = new Map();
  for (const x of items) {
    if (x.parent && ids.has(x.parent)) {
      if (!children.has(x.parent)) children.set(x.parent, []);
      children.get(x.parent).push(x);
    }
  }
  const rows = [];
  const walk = (item, depth) => {
    rows.push({ item, depth });
    for (const child of children.get(item.id) ?? []) walk(child, depth + 1);
  };
  for (const x of items) {
    if (!(x.parent && ids.has(x.parent))) walk(x, 0);
  }
  return rows;
}

export function buildItems(answer, route, kinds) {
  const groups = groupTags(route.tags ?? []);
  const q = route.q ?? '';
  const hit = answer.items.filter((x) => hasText(x, q) && matchesTags(x, groups));
  const filters = answer.model.tags.map((tag) => ({
    name: tag.name,
    values: tag.values,
    selected: groups.get(tag.name) ?? [],
  }));
  return {
    kind: answer.kind,
    name: answer.model.name,
    kinds,
    q,
    tags: route.tags ?? [],
    filters,
    tagNames: answer.model.tags.map((t) => t.name),
    rows: orderRows(hit),
    count: hit.length,
    total: answer.items.length,
    isFiltered: Boolean(q) || groups.size > 0,
    isEmptyKind: answer.items.length === 0,
    isNoMatch: answer.items.length > 0 && hit.length === 0,
  };
}

// 거르기 단추 하나를 켜고 끈 뒤의 tag 목록.
export function toggleTag(tags, name, value) {
  const has = tags.some((t) => t.name === name && t.value === value);
  return has ? tags.filter((t) => !(t.name === name && t.value === value)) : [...tags, { name, value }];
}
