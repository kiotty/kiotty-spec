// 열기 실패를 보일 문장으로 (UI-169 오류 표시). error 는 {code, repo, ref, path} 또는 SpecError.

import { format } from '../../core/format.js';
import { OPEN_ERRORS, TEXT } from '../../i18n/ko.js';

const LOCAL_HINTED = new Set(['REPO_NOT_FOUND', 'REF_NOT_FOUND', 'PATH_NOT_FOUND', 'NOT_FOUND']);

function templateOf(error) {
  if (error.code === 'FORMAT' && error.line) return OPEN_ERRORS.FORMAT_LINE;
  return OPEN_ERRORS[error.code] ?? OPEN_ERRORS.OTHER;
}

export function describeOpenError(error, link) {
  const repo = link?.repo ?? '';
  const ref = link?.ref ?? TEXT.defaultBranch;
  const path = link?.path ?? '';
  const detail = error.code === 'FORMAT' ? error.line : error.message ?? error.code;
  return {
    text: format(templateOf(error), repo, ref, path, detail),
    local: LOCAL_HINTED.has(error.code),
    retry: error.code === 'NETWORK',
  };
}
