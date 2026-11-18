// 항목 표 · 항목 상세 · 변경 요청 보드 (UI-173, UI-174, UI-175). 본문은 data-md 자리에 markdown 으로 채운다.

import { esc } from '../../core/html.js';
import { format } from '../../core/format.js';
import { COLUMNS, TEXT } from '../../i18n/ko.js';
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
    <select data-act="kind" aria-label="${TEXT.kind}">${view.kinds.map(option).join('')}</select>
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
      ? `<tr class="bodyrow"><td colspan="${columns}"><div class="md" data-md="${esc(item.id)}"></div><a class="more" href="${formatItemRoute(item.id)}">${TEXT.detailLink}</a></td></tr>`
      : '';
    return `<tr class="row ${isOpen ? 'open' : ''}" data-toggle="${esc(item.id)}">
      <td data-l="${COLUMNS.id}" class="nowrap indent-${Math.min(depth, 4)}"><span class="caret">${isOpen ? '▾' : '▸'}</span> ${itemLink(item.id)}</td>
      <td data-l="${COLUMNS.title}" class="ttl">${esc(item.title)}</td><td data-l="${COLUMNS.summary}" class="muted">${esc(dash(item.summary))}</td>
      ${tagCells}<td data-l="${COLUMNS.checkpoint}" class="nowrap muted">${esc(item.checkpoint ?? '-')}</td></tr>${body}`;
  }).join('');
}

export function renderItems(view, { expanded }) {
  const clear = formatItemsRoute(view.kind);
  let table;
  if (view.isEmptyKind) table = `<div class="empty">${TEXT.emptyKind}</div>`;
  else if (view.isNoMatch) table = `<div class="empty">${TEXT.noMatch} · <a href="${clear}">${TEXT.clearFilter}</a></div>`;
  else {
    table = `<table class="rt items"><thead><tr><th>${COLUMNS.id}</th><th>${COLUMNS.title}</th><th>${COLUMNS.summary}</th>${view.tagNames.map((t) => `<th>${esc(t)}</th>`).join('')}<th>${COLUMNS.checkpoint}</th></tr></thead>
    <tbody>${itemRows(view, expanded)}</tbody></table>`;
  }
  return `<main class="page items">
  ${kindList(view)}
  <section class="itembox">
    <div class="ihead"><h2>${esc(view.name)}</h2><code class="muted">${esc(view.kind)}</code></div>
    <div class="toolbar">
      <input type="search" data-act="q" placeholder="${TEXT.searchPlaceholder}" value="${esc(view.q)}" aria-label="${TEXT.search}">
      ${view.filters.map(filterMenu).join('')}
      <span class="count-badge">${view.count} / ${view.total}</span>
      ${view.isFiltered ? `<a href="${clear}" class="clear">${TEXT.clearFilter}</a>` : ''}
    </div>
    ${table}
  </section>
</main>`;
}

export function renderUnknownKind(kind) {
  return `<main class="page"><section class="card empty"><h2>${esc(format(TEXT.unknownKind, kind))}</h2><p><a href="#/items/requirement">${TEXT.toRequirements}</a></p></section></main>`;
}

export function renderMissingItem(id, lastKind) {
  return `<main class="page"><section class="card empty"><h2>${esc(format(TEXT.missingItem, id))}</h2><p><a href="${formatItemsRoute(lastKind || 'requirement')}">${TEXT.toItems}</a></p></section></main>`;
}

function relationGroups(groups) {
  if (groups.length === 0) return `<p class="muted">${TEXT.none}</p>`;
  return groups.map((g) => `<div class="relgroup"><div class="relname">${esc(g.name)} <small>${g.list.length}</small></div><ul>${g.list.map((r) => `<li>${itemRef(r)}</li>`).join('')}</ul></div>`).join('');
}

function plainList(rows) {
  return rows.length ? `<ul class="plain">${rows.map((r) => `<li>${itemRef(r)}</li>`).join('')}</ul>` : `<p class="muted">${TEXT.none}</p>`;
}

export function renderItem(view, kindName) {
  const it = view.item;
  const parent = it.parent ? `<p class="muted">${TEXT.parent}: ${itemLink(it.parent)} <span class="ititle">${esc(it.parent_title)}</span></p>` : '';
  const tags = view.tags.length
    ? `<div class="tags">${view.tags.map((t) => `<div><span class="muted">${esc(t.name)}</span> ${t.values.map((v) => `<span class="tag">${esc(v)}</span>`).join(' ')}</div>`).join('')}</div>`
    : '';
  const body = view.body ? `<section class="card md" data-md="${esc(it.id)}"></section>` : `<section class="card"><p class="muted">${TEXT.noBody}</p></section>`;
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
      <section class="card"><h2>${view.isChangeRequest ? TEXT.outOfChange : TEXT.outOfItem}</h2>${relationGroups(view.out)}</section>
      <section class="card"><h2>${TEXT.intoItem}</h2>${relationGroups(view.in)}</section>
      <section class="card"><h2>${TEXT.impact}</h2>${plainList(view.impact)}</section>
      <section class="card"><h2>${TEXT.children}</h2>${plainList(view.children)}</section>
    </div>
  </div>
</main>`;
}

function card(c) {
  const meta = [
    ...c.states.map((v) => `<span class="tag st">${esc(v)}</span>`),
    ...c.sources.map((v) => `<span class="pill">${esc(v)}</span>`),
    ...c.entries.map((v) => `<span class="pill">${esc(v)}</span>`),
    `<span class="impactn" title="${TEXT.impact}">↳ ${c.outCount}</span>`,
  ].join('');
  return `<a class="kcard" href="${formatItemRoute(c.id)}"><span class="iid">${esc(c.id)}</span><b class="ktitle">${esc(c.title)}</b>
    ${dash(c.summary) ? `<span class="ksum">${esc(c.summary)}</span>` : ''}<span class="kmeta">${meta}</span></a>`;
}

export function renderChanges(view) {
  if (view.isEmpty) return `<main class="page"><section class="card empty"><h2>${TEXT.noChanges}</h2></section></main>`;
  const column = (c) => `<section class="kcol kcol-${c.key}">
    <header><h2>${esc(c.title)}</h2><span class="count-badge">${c.count}</span></header>
    <div class="kcards">${c.cards.map(card).join('') || `<p class="muted kempty">${TEXT.none}</p>`}
    ${c.hidden > 0 ? `<button class="btn ghost more-cards" data-more="${c.key}">${format(TEXT.moreCards, c.hidden)}</button>` : ''}
    ${c.canFold ? `<button class="btn ghost more-cards" data-more="${c.key}">${TEXT.fold}</button>` : ''}</div>
  </section>`;
  return `<main class="page wide"><div class="board">${view.columns.map(column).join('')}</div></main>`;
}
