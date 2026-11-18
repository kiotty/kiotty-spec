// 화면 고르기와 입력 (DSN-124). 상태나 # 가 바뀌면 지금 화면만 다시 그린다.

import { buildOverview } from '../viewModel/overview.js';
import { buildProgress } from '../viewModel/progress.js';
import { buildItems, toggleTag } from '../viewModel/items.js';
import { buildBoard, buildItem } from '../viewModel/changes.js';
import { parse, render as renderMarkdown } from '../../core/markdown.js';
import { formatLink, isRepoName } from '../../datalayer/datasource/github_source.js';
import { TEXT } from '../../i18n/ko.js';
import { formatItemRoute, formatItemsRoute, parseRoute } from './router.js';
import { renderShell } from './shell.js';
import { renderOpen, renderOverview, renderProgress, renderUnsupported } from './screens.js';
import { renderChanges, renderItem, renderItems, renderMissingItem, renderUnknownKind } from './item_screens.js';

export function createApp({ root, specState, localSource, location, history, document }) {
  const ui = {
    stepFilter: 0,
    lastKind: 'requirement',
    form: { repo: '', ref: '', path: '' },
    expanded: new Set(),      // 항목 표에서 펼친 줄 (다시 읽어도 남는다)
    moreColumns: new Set(),   // 보드에서 더 보기를 누른 열
    bodies: new Map(),        // 이번 그리기에 채울 본문 (data-md 키 → 글)
    restore: null,            // 권한을 다시 물어야 하는 보관 핸들 {needsPermission, name}
  };

  function sourceName(state) {
    return state.source?.type === 'repo' ? state.source.repo : state.source?.name;
  }

  function query(request) {
    try {
      return specState.query(request);
    } catch (error) {
      if (error.status === 'NOT_FOUND') return null;
      throw error;
    }
  }

  function kindsOf() {
    return query('overview').kinds;
  }

  function renderItemsScreen(route) {
    const answer = query(`items:${route.kind}`);
    if (!answer) return renderUnknownKind(route.kind);
    ui.lastKind = route.kind;
    const view = buildItems(answer, route, kindsOf());
    for (const { item } of view.rows) {
      if (ui.expanded.has(item.id)) ui.bodies.set(item.id, query(`item:${item.id}`)?.body ?? '');
    }
    return renderItems(view, ui);
  }

  function renderItemScreen(route) {
    const answer = query(`item:${route.id}`);
    if (!answer) return renderMissingItem(route.id, ui.lastKind);
    const view = buildItem(answer);
    ui.bodies.set(view.item.id, view.body);
    const kindName = kindsOf().find((k) => k.kind === view.item.kind)?.name ?? view.item.kind;
    return renderItem(view, kindName);
  }

  function renderScreen(state, route) {
    if (state.openError?.error?.code === 'UNSUPPORTED') return renderUnsupported();
    const isOpen = Boolean(state.project);
    if (route.screen === 'open' || (!isOpen && state.phase !== 'loading')) {
      return renderOpen({ openError: state.openError, form: ui.form, restore: ui.restore });
    }
    if (!isOpen) return '';
    switch (route.screen) {
      case 'overview': return renderOverview(buildOverview(query('overview'), sourceName(state)));
      case 'items': return renderItemsScreen(route);
      case 'item': return renderItemScreen(route);
      case 'changes': return renderChanges(buildBoard(query('changes'), { expanded: ui.moreColumns }));
      default: return renderProgress(buildProgress(query('progress'), ui), ui);
    }
  }

  function fillBodies() {
    for (const box of root.querySelectorAll('[data-md]')) {
      const text = ui.bodies.get(box.dataset.md) ?? '';
      if (text) box.appendChild(renderMarkdown(parse(text), document, { itemHref: formatItemRoute }));
      else box.innerHTML = '<p class="muted">본문 없음</p>';
    }
    ui.bodies.clear();
  }

  function render() {
    const state = specState.get();
    const route = parseRoute(location.hash);
    const focused = document.activeElement?.dataset?.act === 'q' ? document.activeElement.selectionStart : null;
    ui.bodies.clear();
    const screen = renderScreen(state, route);
    root.innerHTML = renderShell({ state, screen: route.screen, lastKind: ui.lastKind }) + screen;
    fillBodies();
    if (focused !== null) {
      const input = root.querySelector('[data-act="q"]');
      input?.focus();
      input?.setSelectionRange(focused, focused);
    }
  }

  function goItems(patch) {
    const route = parseRoute(location.hash);
    const next = { tags: route.tags, q: route.q, ...patch };
    history.replaceState(null, '', formatItemsRoute(route.kind, next));
    render();
  }

  function submitOpen(form) {
    const data = new FormData(form);
    ui.form = { repo: String(data.get('repo') ?? '').trim(), ref: String(data.get('ref') ?? ''), path: String(data.get('path') ?? '') };
    if (!isRepoName(ui.form.repo)) {
      root.querySelector('#formerr').textContent = TEXT.repoFormat;
      return;
    }
    location.assign(`${location.pathname}${formatLink(ui.form)}#/progress`);
  }

  root.addEventListener('submit', (event) => {
    if (event.target.dataset.form !== 'open') return;
    event.preventDefault();
    submitOpen(event.target);
  });
  root.addEventListener('input', (event) => {
    if (event.target.dataset.act === 'q') goItems({ q: event.target.value });
  });
  root.addEventListener('change', (event) => {
    const target = event.target;
    if (target.dataset.act === 'cpstep') {
      ui.stepFilter = Number(target.value);
      render();
    } else if (target.dataset.act === 'kind') {
      location.hash = formatItemsRoute(target.value);
    } else if (target.dataset.tagName !== undefined) {
      const route = parseRoute(location.hash);
      goItems({ tags: toggleTag(route.tags, target.dataset.tagName, target.dataset.tagValue) });
      root.querySelector(`[data-filter="${CSS.escape(target.dataset.tagName)}"]`)?.setAttribute('open', '');
    }
  });
  // 로컬 파일을 열면 주소에서 repo 를 지우고 진행 화면으로 간다 (UI-170). 이미 보던 화면이면 그대로 둔다.
  async function openLocal(result, notice) {
    if (!result) return;
    ui.restore = null;
    const route = parseRoute(location.hash);
    await specState.openLocal(result.source, notice);
    if (specState.get().source?.type !== 'local') return;
    const hash = route.screen === 'open' || !location.hash ? '#/progress' : location.hash;
    history.replaceState(null, '', `${location.pathname}${hash}`);
    render();
  }

  async function pick() {
    const result = await localSource.pick();
    await openLocal(result, result?.isBlocked ? { text: TEXT.blockedPick } : null);
  }

  async function reopen() {
    try {
      await openLocal({ source: await localSource.reopen() }, null);
    } catch {
      ui.restore = null;
      specState.notify({ text: TEXT.lostHandle });
    }
  }

  root.addEventListener('keydown', (event) => {
    const isPick = event.target.dataset?.act === 'pick' && (event.key === 'Enter' || event.key === ' ');
    if (isPick) { event.preventDefault(); pick(); }
  });
  root.addEventListener('click', (event) => {
    if (event.target.closest('[data-act="pick"]')) { pick(); return; }
    if (event.target.closest('[data-act="reopen"]')) { reopen(); return; }
    const retry = event.target.closest('[data-act="retry"]');
    if (retry) {
      const link = specState.get().openError?.link;
      if (link) specState.openRepo(link);
      return;
    }
    const more = event.target.closest('[data-more]');
    if (more) {
      const key = more.dataset.more;
      if (ui.moreColumns.has(key)) ui.moreColumns.delete(key);
      else ui.moreColumns.add(key);
      render();
      return;
    }
    const row = event.target.closest('tr[data-toggle]');
    if (row && !event.target.closest('a')) {
      const id = row.dataset.toggle;
      if (ui.expanded.has(id)) ui.expanded.delete(id);
      else ui.expanded.add(id);
      render();
    }
  });

  return {
    render,
    async openDropped(dataTransfer) {
      const result = await localSource.fromDrop(dataTransfer);
      await openLocal(result, result?.isMany ? { text: TEXT.manyFiles } : null);
    },
    async restoreLocal() {
      const result = await localSource.restore();
      if (result?.source) await openLocal(result, null);
      else if (result?.needsPermission) { ui.restore = result; render(); }
    },
    fillForm(link) {
      if (link) ui.form = { repo: link.repo, ref: link.ref ?? '', path: link.path };
    },
    showProgressIfEmpty() {
      if (!location.hash || location.hash === '#' || location.hash === '#/open') {
        history.replaceState(null, '', '#/progress');
      }
    },
  };
}
