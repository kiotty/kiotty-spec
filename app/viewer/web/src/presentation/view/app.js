// 화면 고르기와 입력 (DSN-124). 상태나 # 가 바뀌면 지금 화면만 다시 그린다.

import { buildOverview } from '../viewModel/overview.js';
import { buildProgress } from '../viewModel/progress.js';
import { formatLink, isRepoName } from '../../datalayer/datasource/github_source.js';
import { TEXT } from '../../i18n/ko.js';
import { parseRoute } from './router.js';
import { renderShell } from './shell.js';
import { renderNotReady, renderOpen, renderOverview, renderProgress, renderUnsupported } from './screens.js';

export function createApp({ root, specState, location, history }) {
  const ui = { stepFilter: 0, lastKind: 'requirement', form: { repo: '', ref: '', path: '' } };

  function sourceName(state) {
    return state.source?.type === 'repo' ? state.source.repo : state.source?.name;
  }

  function renderScreen(state, route) {
    if (state.openError?.error?.code === 'UNSUPPORTED') return renderUnsupported();
    const isOpen = Boolean(state.project);
    if (route.screen === 'open' || (!isOpen && state.phase !== 'loading')) {
      return renderOpen({ openError: state.openError, form: ui.form });
    }
    if (!isOpen) return '';
    if (route.screen === 'overview') {
      return renderOverview(buildOverview(specState.query('overview'), sourceName(state)));
    }
    if (route.screen === 'progress') {
      return renderProgress(buildProgress(specState.query('progress'), ui), ui);
    }
    if (route.screen === 'items') ui.lastKind = route.kind;
    return renderNotReady();
  }

  function render() {
    const state = specState.get();
    const route = parseRoute(location.hash);
    root.innerHTML = renderShell({ state, screen: route.screen, lastKind: ui.lastKind }) + renderScreen(state, route);
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
  root.addEventListener('change', (event) => {
    if (event.target.dataset.act !== 'cpstep') return;
    ui.stepFilter = Number(event.target.value);
    render();
  });
  root.addEventListener('click', (event) => {
    const button = event.target.closest('[data-act="retry"]');
    if (!button) return;
    const link = specState.get().openError?.link;
    if (link) specState.openRepo(link);
  });

  return {
    render,
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
