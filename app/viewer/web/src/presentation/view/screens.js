// 열기 · 개요 · 진행 화면 (UI-170, UI-171, UI-172). 모든 사양 글은 esc 를 거친다.

import { esc, formatTime } from '../../core/html.js';
import { describeOpenError, STEPS, TEXT } from '../../i18n/ko.js';
import { DEFAULT_PATH } from '../../datalayer/datasource/github_source.js';
import { formatItemRoute } from './router.js';

function itemLink(id) {
  return `<a class="iid" href="${formatItemRoute(id)}">${esc(id)}</a>`;
}

export function renderOpen({ openError, form, restore }) {
  const described = openError ? describeOpenError(openError.error, openError.link) : null;
  const alert = described
    ? `<div class="alert" role="alert">${esc(described.text)}${described.local ? ` ${esc(TEXT.localHint)}` : ''}
       ${described.retry ? ` <button class="btn small" data-act="retry">${TEXT.retry}</button>` : ''}</div>`
    : '';
  const reopen = restore?.needsPermission
    ? `<section class="card"><button class="btn" data-act="reopen">${TEXT.reopen}: ${esc(restore.name)}</button></section>`
    : '';
  return `<main class="page narrow">
  ${alert}
  ${reopen}
  <section class="card">
    <h2>${TEXT.openRepoTitle}</h2>
    <form data-form="open" class="form" novalidate>
      <label>${TEXT.repoLabel}<input name="repo" placeholder="OWNER/NAME" value="${esc(form.repo)}" autocomplete="off"></label>
      <label>${TEXT.refLabel}<input name="ref" placeholder="${esc(TEXT.refPlaceholder)}" value="${esc(form.ref)}" autocomplete="off"></label>
      <details ${form.path && form.path !== DEFAULT_PATH ? 'open' : ''}><summary>${TEXT.pathLabel}</summary>
        <label><input name="path" placeholder="${DEFAULT_PATH}" value="${esc(form.path === DEFAULT_PATH ? '' : form.path)}" autocomplete="off"></label></details>
      <div class="formerr" id="formerr"></div>
      <div><button class="btn" type="submit">${TEXT.openButton}</button></div>
    </form>
  </section>
  <section class="card">
    <h2>${TEXT.openLocalTitle}</h2>
    <div class="drop" data-act="pick" role="button" tabindex="0"><span>${TEXT.dropHere}</span><span class="btn">${TEXT.pickFile}</span></div>
    <p class="muted">${TEXT.localPrivacy}</p>
  </section>
</main>`;
}

export function renderUnsupported() {
  return `<main class="page narrow"><section class="card empty"><h2>${TEXT.unsupported}</h2><p class="muted">${TEXT.supported}</p></section></main>`;
}

export function renderOverview(view) {
  return `<main class="page">
  <section class="hero">
    <h1>${esc(view.title)}</h1>
    <p class="muted">${TEXT.createdAt} ${formatTime(view.createdAt)} · ${TEXT.itemCount} ${view.itemCount} 개</p>
  </section>
  <div class="grid2">
    <section class="card"><h2>${TEXT.purpose}</h2><div class="pre">${esc(view.purpose)}</div></section>
    <section class="card"><h2>${TEXT.roles}</h2><div class="pre">${esc(view.roles)}</div>
      <h2 class="mt">${TEXT.counts}</h2>
      <div class="counts">${view.kinds.map((k) => `<a href="#/items/${encodeURIComponent(k.kind)}" class="count ${k.isZero ? 'zero' : ''}"><b>${k.count}</b><span>${esc(k.name)}</span><small>${esc(k.prefix)}</small></a>`).join('')}</div>
    </section>
  </div>
  <section class="card"><h2>${TEXT.steps}</h2>
    <table class="rt"><thead><tr><th>단계</th><th>이름</th><th>하는 일</th><th>항목</th></tr></thead><tbody>
    ${view.steps.map((s) => `<tr><td data-l="단계">${s.step}</td><td data-l="이름"><b>${esc(s.name)}</b></td><td data-l="하는 일">${esc(s.does)}</td><td data-l="항목">${esc(s.kinds)}</td></tr>`).join('')}
    </tbody></table>
  </section>
</main>`;
}

export function renderProgress(view, { stepFilter }) {
  if (view.isEmpty) {
    return `<main class="page"><section class="card empty"><h2>${TEXT.noProgress}</h2><p class="muted">${TEXT.noProgressHint}</p></section></main>`;
  }
  const cur = view.current;
  const chips = `<ol class="steps">${cur.chips.map((c) => `<li class="${c.mark}"><span>${c.step}</span>${esc(c.name)}</li>`).join('')}</ol>`;
  const unmet = view.unmet.length
    ? `<section class="card"><h2>${TEXT.unmetTitle} <span class="count-badge">${view.unmet.length}</span></h2>
    <table class="rt"><thead><tr><th>ID</th><th>종류</th><th>제목</th><th>이유</th></tr></thead><tbody>
    ${view.unmet.map((u) => `<tr><td data-l="ID">${itemLink(u.id)}</td><td data-l="종류">${esc(u.kind)}</td><td data-l="제목">${esc(u.title)}</td><td data-l="이유"><span class="warn">${esc(u.reason)}</span></td></tr>`).join('')}
    </tbody></table></section>`
    : '';
  const options = STEPS.map((s) => `<option value="${s.step}" ${stepFilter === s.step ? 'selected' : ''}>${s.step} ${esc(s.name)}</option>`).join('');
  const rows = view.checkpoints.map((c) => `<tr><td data-l="CP"><b>${esc(c.checkpoint)}</b></td><td data-l="단계">${c.step} ${esc(c.stepName)}</td><td data-l="상태"><span class="pill state-${esc(c.state)}">${esc(c.stateName)}</span></td><td data-l="설명">${esc(c.description)}</td><td data-l="시각" class="nowrap">${formatTime(c.created_at)}</td></tr>`).join('');
  return `<main class="page">
  <p class="hint">${esc(TEXT.firstHint).replace('개요', '<a href="#/overview">개요</a>')}</p>
  <section class="card current">
    <div class="cprow"><h2>${esc(cur.checkpoint)}</h2><span class="pill">${cur.step} ${esc(cur.stepName)}</span><span class="pill state-${esc(cur.state)}">${esc(cur.stateName)}</span></div>
    <p>${esc(cur.description)}</p>
    ${chips}
    ${view.status ? `<p class="muted"><b>${esc(view.status)}</b></p>` : ''}
  </section>
  ${unmet}
  <section class="card"><div class="cprow"><h2>${TEXT.checkpointsTitle}</h2><span class="grow"></span>
    <select data-act="cpstep" aria-label="단계 거르기"><option value="0">${TEXT.everyStep}</option>${options}</select></div>
    <table class="rt"><thead><tr><th>CP</th><th>단계</th><th>상태</th><th>설명</th><th>시각</th></tr></thead><tbody>
    ${rows || `<tr><td colspan="5" class="muted">${TEXT.noCheckpointInStep}</td></tr>`}
    </tbody></table></section>
</main>`;
}

