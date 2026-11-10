"""#kisp 1 파일과 본문 디렉토리 (.kiotty/spec.kisp + .kiotty/spec/<key>.md) 를 #kisp 2 파일 하나로 바꾼다.

    python tools/kisp_inline_bodies.py <.kisp 경로>

본문은 파일 끝의 body 표에 key 순서로 한 줄씩 들어간다. 본문 디렉토리는 지우지 않는다 (확인한 뒤 손으로 지운다).
item 줄이 없는 본문 파일이 있거나 이미 #kisp 2 이면 멈춘다.
"""

import os
import sys

OLD_HEADER = '#kisp 1 model 1\n'
NEW_HEADER = '#kisp 2 model 1\n'
BODY_HEADER = 'TABLE\tbody\tO/X\tkey\ttext\n'
ESCAPES = {'\\': '\\\\', '\t': '\\t', '\n': '\\n', '\r': '\\r'}


def escape(text):
    return ''.join(ESCAPES.get(letter, letter) for letter in text)


def item_keys(lines):
    keys = set()
    for line in lines:
        fields = line.rstrip('\n').split('\t')
        if fields[0] == 'item':
            keys.add(fields[2])
    return keys


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    with open(path, 'rb') as stream:
        lines = stream.read().decode('utf-8').splitlines(keepends=True)
    if not lines or lines[0] != OLD_HEADER:
        sys.exit('첫 줄이 ' + OLD_HEADER.strip() + ' 이 아님: ' + path)
    bodies = os.path.splitext(path)[0]
    keys = item_keys(lines)
    out = [NEW_HEADER] + lines[1:] + [BODY_HEADER]
    names = sorted(name for name in os.listdir(bodies) if name.endswith('.md')) if os.path.isdir(bodies) else []
    for name in names:
        key = name[:-3]
        if key not in keys:
            sys.exit('item 줄이 없는 본문 파일: ' + os.path.join(bodies, name))
        with open(os.path.join(bodies, name), 'rb') as stream:
            text = stream.read().decode('utf-8')
        if text:
            out.append('body\tO\t%s\t%s\n' % (key, escape(text)))
    with open(path, 'wb') as stream:
        stream.write(''.join(out).encode('utf-8'))
    print('본문 %d 개를 옮김: %s' % (len(names), path))


if __name__ == '__main__':
    main()
