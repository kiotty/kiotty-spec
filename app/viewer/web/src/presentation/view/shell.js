// 머리 줄 · 탭 · 불러오는 중 (UI-168).

import { esc } from '../../core/html.js';
import { describeOpenError, TEXT } from '../../i18n/ko.js';

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

function clock(ms) {
  const date = new Date(ms);
  const pad = (n) => String(n).padStart(2, '0');
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

// 로컬 파일 지켜보기 배지와 다시 읽음 · 다시 읽지 못함 (UI-168, REQ-054).
function watchBadge(watch) {
  if (!watch) return '';
  const isAuto = watch.mode === 'observer' || watch.mode === 'poll';
  const badge = `<span class="pill ${isAuto ? 'state-done' : ''}" title="${esc(watch.mode)}">${isAuto ? TEXT.watchAuto : TEXT.watchManual}</span>`;
  if (watch.error) return `${badge} <span class="pill state-running" role="status">${TEXT.reloadFailed}: ${esc(describeError(watch.error))}</span>`;
  if (watch.reloadedAt) return `${badge} <span class="pill" role="status">${TEXT.reloadedAt} ${clock(watch.reloadedAt)}</span>`;
  return badge;
}

function describeError(error) {
  return describeOpenError(error, null).text;
}

function noticeBar(notice) {
  if (!notice) return '';
  if (notice.error) return `<div class="notice alert" role="alert">${esc(notice.name)}: ${TEXT.notOpened} — ${esc(describeError(notice.error))}</div>`;
  return `<div class="notice alert" role="status">${esc(notice.text)}</div>`;
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
    ${watchBadge(state.watch)}
    <span class="grow"></span>
    <a class="btn ghost" href="#/open">${TEXT.openOther}</a>
  </div>
  ${tabs}
  ${loading}
</header>
${noticeBar(state.notice)}`;
}
