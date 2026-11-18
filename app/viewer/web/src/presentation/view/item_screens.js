// 항목 표 · 항목 상세 · 변경 요청 보드 (UI-173, UI-174, UI-175). 본문은 data-md 자리에 markdown 으로 채운다.

import { esc } from '../../core/html.js';
import { formatItemRoute, formatItemsRoute } from './router.js';

const dash = (text) => (text && text !== '-' ? text : '');

function itemLink(id) {
  return `<a class="iid" href="${formatItemRoute(id)}">${esc(id)}</a>`;
}

function itemRef(row) {
  return `${itemLink(row.id)} <span class="ititle">${esc(row.title)}</span>`;
}

function kindList(view) {
  const option = (k) => `<option value="${esc(k.kind)}" ${k.kind === view.kind ? 'selected' : ''}>${esc(k.name)} (${k.count})</option>`;
  const link = (k) => `<li><a href="#/items/${encodeURIComponent(k.kind)}" class="${k.kind === view.kind ? 'on' : ''} ${k.count ? '' : 'zero'}"><span>${esc(k.name)}</span><small>${k.count}</small></a></li>`;
  return `<aside class="kinds">
    <select data-act="kind" aria-label="종류">${view.kinds.map(option).join('')}</select>
    <ul>${view.kinds.map(link).join('')}</ul>
  </aside>`;
}

function filterMenu(filter) {
  const picked = filter.selected;
  const label = picked.length ? `${esc(filter.name)}: ${esc(picked.join(', '))}` : esc(filter.name);
  const boxes = filter.values.map((v) => `<label><input type="checkbox" data-tag-name="${esc(filter.name)}" data-tag-value="${esc(v)}" ${picked.includes(v) ? 'checked' : ''}> ${esc(v)}</label>`).join('');
  return `<details class="filter ${picked.length ? 'set' : ''}" data-filter="${esc(filter.name)}"><summary>${label}</summary><div class="menu">${boxes}</div></details>`;
}

function itemRows(view, expanded) {
  const columns = 4 + view.tagNames.length;
  return view.rows.map(({ item, depth }) => {
    const isOpen = expanded.has(item.id);
    const tagCells = view.tagNames.map((name) => `<td data-l="${esc(name)}">${(item.tags[name] ?? []).map((v) => `<span class="tag">${esc(v)}</span>`).join(' ')}</td>`).join('');
    const body = isOpen
      ? `<tr class="bodyrow"><td colspan="${columns}"><div class="md" data-md="${esc(item.id)}"></div><a class="more" href="${formatItemRoute(item.id)}">상세 보기 →</a></td></tr>`
      : '';
    return `<tr class="row ${isOpen ? 'open' : ''}" data-toggle="${esc(item.id)}">
      <td data-l="ID" class="nowrap indent-${Math.min(depth, 4)}"><span class="caret">${isOpen ? '▾' : '▸'}</span> ${itemLink(item.id)}</td>
      <td data-l="제목" class="ttl">${esc(item.title)}</td><td data-l="요약" class="muted">${esc(dash(item.summary))}</td>
      ${tagCells}<td data-l="체크포인트" class="nowrap muted">${esc(item.checkpoint ?? '-')}</td></tr>${body}`;
  }).join('');
}

export function renderItems(view, { expanded }) {
  const clear = formatItemsRoute(view.kind);
  let table;
  if (view.isEmptyKind) table = '<div class="empty">이 종류의 항목이 없습니다</div>';
  else if (view.isNoMatch) table = `<div class="empty">맞는 항목이 없습니다 · <a href="${clear}">거르기 지우기</a></div>`;
  else {
    table = `<table class="rt items"><thead><tr><th>ID</th><th>제목</th><th>요약</th>${view.tagNames.map((t) => `<th>${esc(t)}</th>`).join('')}<th>체크포인트</th></tr></thead>
    <tbody>${itemRows(view, expanded)}</tbody></table>`;
  }
  return `<main class="page items">
  ${kindList(view)}
  <section class="itembox">
    <div class="ihead"><h2>${esc(view.name)}</h2><code class="muted">${esc(view.kind)}</code></div>
    <div class="toolbar">
      <input type="search" data-act="q" placeholder="제목 · 요약에서 찾기" value="${esc(view.q)}" aria-label="찾기">
      ${view.filters.map(filterMenu).join('')}
      <span class="count-badge">${view.count} / ${view.total}</span>
      ${view.isFiltered ? `<a href="${clear}" class="clear">거르기 지우기</a>` : ''}
    </div>
    ${table}
  </section>
</main>`;
}

export function renderUnknownKind(kind) {
  return `<main class="page"><section class="card empty"><h2>없는 종류: ${esc(kind)}</h2><p><a href="#/items/requirement">요구사항 표로</a></p></section></main>`;
}

export function renderMissingItem(id, lastKind) {
  return `<main class="page"><section class="card empty"><h2>없는 항목: ${esc(id)}</h2><p><a href="${formatItemsRoute(lastKind || 'requirement')}">항목 표로</a></p></section></main>`;
}

function relationGroups(groups) {
  if (groups.length === 0) return '<p class="muted">(없음)</p>';
  return groups.map((g) => `<div class="relgroup"><div class="relname">${esc(g.name)} <small>${g.list.length}</small></div><ul>${g.list.map((r) => `<li>${itemRef(r)}</li>`).join('')}</ul></div>`).join('');
}

function plainList(rows) {
  return rows.length ? `<ul class="plain">${rows.map((r) => `<li>${itemRef(r)}</li>`).join('')}</ul>` : '<p class="muted">(없음)</p>';
}

export function renderItem(view, kindName) {
  const it = view.item;
  const parent = it.parent ? `<p class="muted">상위: ${itemLink(it.parent)} <span class="ititle">${esc(it.parent_title)}</span></p>` : '';
  const tags = view.tags.length
    ? `<div class="tags">${view.tags.map((t) => `<div><span class="muted">${esc(t.name)}</span> ${t.values.map((v) => `<span class="tag">${esc(v)}</span>`).join(' ')}</div>`).join('')}</div>`
    : '';
  const body = view.body ? `<section class="card md" data-md="${esc(it.id)}"></section>` : '<section class="card"><p class="muted">본문 없음</p></section>';
  return `<main class="page detail">
  <div class="crumb"><a href="${formatItemsRoute(it.kind)}">${esc(kindName)}</a>${it.parent ? ` › ${itemLink(it.parent)}` : ''} › <b>${esc(it.id)}</b></div>
  <section class="card">
    <div class="cprow"><span class="iid big">${esc(it.id)}</span><span class="pill">${esc(kindName)}</span><span class="pill">${esc(it.checkpoint ?? '-')}</span></div>
    <h1>${esc(it.title)}</h1>
    ${dash(it.summary) ? `<p class="summary">${esc(it.summary)}</p>` : ''}
    ${parent}
    ${tags}
  </section>
  <div class="grid2 detailgrid">
    ${body}
    <div>
      <section class="card"><h2>${view.isChangeRequest ? '영향 항목 (이 변경 요청이 가리키는 것)' : '이 항목이 가리키는 것'}</h2>${relationGroups(view.out)}</section>
      <section class="card"><h2>이 항목을 가리키는 것</h2>${relationGroups(view.in)}</section>
      <section class="card"><h2>영향 항목</h2>${plainList(view.impact)}</section>
      <section class="card"><h2>하위 항목</h2>${plainList(view.children)}</section>
    </div>
  </div>
</main>`;
}

function card(c) {
  const meta = [
    ...c.states.map((v) => `<span class="tag st">${esc(v)}</span>`),
    ...c.sources.map((v) => `<span class="pill">${esc(v)}</span>`),
    ...c.entries.map((v) => `<span class="pill">${esc(v)}</span>`),
    `<span class="impactn" title="영향 항목">↳ ${c.outCount}</span>`,
  ].join('');
  return `<a class="kcard" href="${formatItemRoute(c.id)}"><span class="iid">${esc(c.id)}</span><b class="ktitle">${esc(c.title)}</b>
    ${dash(c.summary) ? `<span class="ksum">${esc(c.summary)}</span>` : ''}<span class="kmeta">${meta}</span></a>`;
}

export function renderChanges(view) {
  if (view.isEmpty) return '<main class="page"><section class="card empty"><h2>변경 요청이 없습니다</h2></section></main>';
  const column = (c) => `<section class="kcol kcol-${c.key}">
    <header><h2>${esc(c.title)}</h2><span class="count-badge">${c.count}</span></header>
    <div class="kcards">${c.cards.map(card).join('') || '<p class="muted kempty">(없음)</p>'}
    ${c.hidden > 0 ? `<button class="btn ghost more-cards" data-more="${c.key}">${c.hidden} 개 더 보기</button>` : ''}
    ${c.canFold ? `<button class="btn ghost more-cards" data-more="${c.key}">접기</button>` : ''}</div>
  </section>`;
  return `<main class="page wide"><div class="board">${view.columns.map(column).join('')}</div></main>`;
}
