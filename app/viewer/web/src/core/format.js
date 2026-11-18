// 문장 치환 (수칙 8): i18n 문장의 {0} {1} … 자리를 값으로 바꾼다. 없는 자리는 빈칸이다.

export function format(template, ...values) {
  return template.replace(/\{(\d+)\}/g, (_, index) => String(values[Number(index)] ?? ''));
}
