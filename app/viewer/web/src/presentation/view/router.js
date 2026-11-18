// router (DSN-124, UI-169): # 뒤를 화면과 매개변수로 바꾼다.

export const SCREENS = ['overview', 'progress', 'items', 'item', 'changes', 'open'];

export function parseRoute(hash) {
  const text = (hash ?? '').replace(/^#/, '');
  const [pathPart, queryPart = ''] = text.split('?');
  const parts = pathPart.split('/').filter(Boolean).map(decodeURIComponent);
  const params = new URLSearchParams(queryPart);
  if (parts.length === 0) return { screen: 'progress' };
  const [screen, arg] = parts;
  if (screen === 'items') {
    const tags = params.getAll('tag').map((entry) => {
      const at = entry.indexOf(':');
      return at < 0 ? { name: entry, value: '' } : { name: entry.slice(0, at), value: entry.slice(at + 1) };
    });
    return { screen, kind: arg ?? 'requirement', tags, q: params.get('q') ?? '' };
  }
  if (screen === 'item') return { screen, id: arg ?? '' };
  if (SCREENS.includes(screen)) return { screen };
  return { screen: 'unknown', path: pathPart };
}

export function formatItemsRoute(kind, { tags = [], q = '' } = {}) {
  const params = new URLSearchParams();
  for (const tag of tags) params.append('tag', `${tag.name}:${tag.value}`);
  if (q) params.set('q', q);
  const query = params.toString();
  return `#/items/${encodeURIComponent(kind)}${query ? `?${query}` : ''}`;
}

export function formatItemRoute(id) {
  return `#/item/${encodeURIComponent(id)}`;
}
