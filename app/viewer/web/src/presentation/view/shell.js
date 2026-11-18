// 머리 줄 · 탭 · 불러오는 중 (UI-168).

import { esc } from '../../core/html.js';
import { TEXT } from '../../i18n/ko.js';

const TABS = ['overview', 'progress', 'items', 'changes'];

function sourceBadge(source) {
  if (!source) return '';
  if (source.type === 'repo') {
    const ref = source.ref ?? '기본 브랜치';
    return `<span class="src" title="${esc(source.path)}">${esc(source.repo)} <b>@</b> ${esc(ref)}</span>`;
  }
  return `<span class="src" title="로컬 파일">📄 ${esc(source.name)}</span>`;
}

function tabHref(tab, lastKind) {
  return tab === 'items' ? `#/items/${encodeURIComponent(lastKind || 'requirement')}` : `#/${tab}`;
}

export function renderShell({ state, screen, lastKind }) {
  const isReady = state.phase === 'ready' || (state.phase !== 'idle' && state.project);
  const active = screen === 'item' ? 'items' : screen;
  const tabs = isReady
    ? `<nav class="tabs">${TABS.map((t) => `<a href="${tabHref(t, lastKind)}" class="${active === t ? 'on' : ''}">${TEXT.tabs[t]}</a>`).join('')}</nav>`
    : '';
  const loading = state.phase === 'loading' ? `<div class="loadbar" role="progressbar" aria-label="${TEXT.loading}"><i></i></div>` : '';
  return `<header class="top">
  <div class="topbar">
    <a class="brand" href="#/progress">${TEXT.brand} <span>${TEXT.brandSub}</span></a>
    ${sourceBadge(state.source)}
    <span class="grow"></span>
    <a class="btn ghost" href="#/open">${TEXT.openOther}</a>
  </div>
  ${tabs}
  ${loading}
</header>`;
}
