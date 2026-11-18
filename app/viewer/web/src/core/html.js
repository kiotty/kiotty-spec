// 화면 글을 HTML 에 넣기 전에 이스케이프한다. 모든 사양 글은 이것을 거친다.

const ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

export function esc(value) {
  return String(value ?? '').replace(/[&<>"']/g, (ch) => ESCAPES[ch]);
}

export function formatTime(seconds) {
  if (seconds === null || seconds === undefined) return '-';
  const date = new Date(Number(seconds) * 1000);
  const pad = (n) => String(n).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}
