"""jsonl (스키마 3, item.jsonl 이 있는 형식) 을 .kisp 텍스트 파일로 바꾼다.

    python tools/kisp_from_jsonl.py <jsonl 디렉토리> <.kisp 경로>

항목의 현재 본문은 파일 끝의 body 표에 쓴다.
더 옛 형식 (스키마 1 · 2 jsonl, SQLite spec.db) 은 커밋 4205e5d 의 ks 로 먼저 `ks import jsonl` · `ks export jsonl` 한다.
모델 1 의 태그 · 관계 이름은 src/implementation/domain/usecases/model/kiotty_model_catalog.cpp 를 옮긴 것이다.
"""

import json
import os
import sys

KINDS = ['change_request', 'requirement', 'rule', 'external_interface', 'design', 'unit_test_plan', 'milestone',
         'unit_test_result', 'system_test_plan', 'system_test_result', 'code_inspection_result', 'e2e_test_plan',
         'e2e_test_result']

PASS_FAIL = ['PASS', 'FAIL']
IMPLEMENTATION = ['미구현', '변경 필요', '구현 완료']
TAGS = {
    'change_request': [('출처', ['사용자 피드백', '이슈', '초기 요구']), ('상태', ['열림', '반영 중', '완료', '거부']),
                       ('진입 단계', ['Step %d' % step for step in range(1, 13)])],
    'requirement': [('요구사항 종류', ['기능 요구사항', '품질 요구사항', '환경 요구사항'])],
    'rule': [('수칙 종류', ['아키텍처', '컨벤션', '코딩 제약', '의존성', '테스트'])],
    'external_interface': [('인터페이스 종류', ['GUI 뷰', 'C 함수', 'C 타입'])],
    'design': [('설계 종류', ['모듈', '동작 흐름', '디렉토리', '메모리 정책']), ('구현 상태', IMPLEMENTATION)],
    'unit_test_plan': [('구현 상태', IMPLEMENTATION)],
    'unit_test_result': [('결과', PASS_FAIL)],
    'system_test_result': [('결과', PASS_FAIL)],
    'code_inspection_result': [('결과', PASS_FAIL)],
    'e2e_test_result': [('결과', PASS_FAIL)],
}
RELATIONS = {
    'change_request': ['영향 요구사항', '영향 개발 수칙', '영향 외부 인터페이스', '영향 설계 사항', '관련 마일스톤'],
    'requirement': ['관련 요구사항'],
    'external_interface': ['관련 요구사항'],
    'design': ['관련 요구사항', '관련 외부 인터페이스', '의존 설계 사항'],
    'unit_test_plan': ['검증 설계 사항'],
    'milestone': ['포함 설계 사항', '포함 유닛 테스트'],
    'unit_test_result': ['대상 유닛 테스트'],
    'system_test_plan': ['검증 요구사항'],
    'system_test_result': ['대상 시스템 테스트'],
    'code_inspection_result': ['검사 개발 수칙'],
    'e2e_test_plan': ['검증 외부 인터페이스'],
    'e2e_test_result': ['대상 E2E 테스트'],
}

HEADERS = [
    'TABLE\tmeta\tO/X\tkey\tvalue',
    'TABLE\tcheckpoint\tO/X\tsequence\tprevious\tstep\tstate\tcreated_at\tdescription',
    'TABLE\titem\tO/X\tkey\tid',
    'TABLE\titem_version\tO/X\tkey\tcheckpoint\tkind\tparent\ttitle\tsummary\tbody',
    'TABLE\ttag\tO/X\tkey\ttag\tcheckpoint\tvalue',
    'TABLE\trelation\tO/X\tfrom\tmodel\trelation\tto\tcheckpoint',
    'TABLE\tdecision\tO/X\tkind\tcheckpoint\ttext',
    'TABLE\tbody\tO/X\tkey\ttext',
]


def fnv64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return value


def mix64(value):
    value ^= value >> 33
    value = (value * 0xff51afd7ed558ccd) & 0xFFFFFFFFFFFFFFFF
    value ^= value >> 33
    value = (value * 0xc4ceb9fe1a85ec53) & 0xFFFFFFFFFFFFFFFF
    value ^= value >> 33
    return value


def escape(text):
    return text.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '\\r')


def line(*fields):
    return '\t'.join(escape(str(field)) for field in fields) + '\n'


def load(directory, table):
    path = os.path.join(directory, table + '.jsonl')
    with open(path, encoding='utf-8') as stream:
        return [json.loads(text) for text in stream if text.strip()]


def make_keys(names):
    keys = {}
    used = set()
    for old_key, item_id in sorted(names.items()):
        mixed = mix64(fnv64(item_id.encode('utf-8')))
        key = (mixed ^ (mixed >> 32)) & 0xFFFFFFFF
        while key == 0 or key in used:
            key = (key + 1) & 0xFFFFFFFF
        used.add(key)
        keys[old_key] = key
    return keys


def key_text(key):
    return '-' if key is None else '%08x' % key


def tag_value(kind, tag, value):
    bits = TAGS[kind][tag][1]
    return ','.join(bits[bit] for bit in range(48) if value & (1 << bit))


def convert_items(rows, keys):
    versions = []
    tags = []
    by_item = {}
    for row in rows:
        by_item.setdefault(row['item_key'], []).append(row)
    for old_key, history in by_item.items():
        history.sort(key=lambda row: row['checkpoint'])
        key = keys[old_key]
        previous_fields = None
        previous_tags = {}
        for row in history:
            body = row['body'].encode('utf-8')
            parent = keys[row['parent_key']] if row['parent_key'] is not None else None
            fields = (row['kind'], parent, row['title'], row['summary'], fnv64(body) if body else 0, row['removed'])
            current_tags = {} if row['removed'] else {tag: value for tag, value in row['tags'] if value}
            changed = []
            if not row['removed']:
                for tag in sorted(set(previous_tags) | set(current_tags)):
                    if previous_tags.get(tag, 0) != current_tags.get(tag, 0):
                        changed.append((tag, current_tags.get(tag, 0)))
            if fields != previous_fields or not changed:
                versions.append((key, row['checkpoint'], fields))
            for tag, value in changed:
                tags.append((key, tag, row['checkpoint'], value, row['kind']))
            previous_fields = fields
            if not row['removed']:
                previous_tags = current_tags
    return sorted(versions, key=lambda version: (version[0], version[1])), sorted(tags, key=lambda tag: tag[:3])


def write_kisp(directory, output):
    meta = load(directory, 'meta')
    checkpoints = load(directory, 'checkpoint')
    names = {row['key']: row['id'] for row in load(directory, 'item')}
    keys = make_keys(names)
    versions, tags = convert_items(load(directory, 'item_version'), keys)
    relations = sorted(((keys[row['from_key']], row['relation'], keys[row['to_key']], row['checkpoint'], row['model'], row['removed'])
                        for row in load(directory, 'relation_version')), key=lambda relation: relation[:4])
    decisions = sorted(load(directory, 'decision_version'), key=lambda row: (KINDS.index(row['kind']), row['checkpoint']))

    out = ['#kisp 2 model 1\n', HEADERS[0] + '\n']
    out += [line('meta', 'O', row['key'], row['value']) for row in sorted(meta, key=lambda row: row['key'].encode('utf-8'))]
    out.append(HEADERS[1] + '\n')
    for row in sorted(checkpoints, key=lambda row: row['sequence']):
        previous = row['previous'] if row['previous'] else '-'
        out.append(line('checkpoint', 'O', row['sequence'], previous, row['step'], row['state'], row['created_at'], row['description']))
    out.append(HEADERS[2] + '\n')
    out += [line('item', 'O', key_text(keys[old_key]), item_id) for old_key, item_id in sorted(names.items(), key=lambda pair: keys[pair[0]])]
    out.append(HEADERS[3] + '\n')
    for key, checkpoint, (kind, parent, title, summary, body_hash, removed) in versions:
        body = '%016x' % body_hash if body_hash else '-'
        out.append(line('item_version', 'X' if removed else 'O', key_text(key), checkpoint, kind, key_text(parent), title, summary, body))
    out.append(HEADERS[4] + '\n')
    for key, tag, checkpoint, value, kind in tags:
        out.append(line('tag', 'O' if value else 'X', key_text(key), TAGS[kind][tag][0], checkpoint, tag_value(kind, tag, value) if value else '-'))
    out.append(HEADERS[5] + '\n')
    for source, relation, target, checkpoint, model, removed in relations:
        out.append(line('relation', 'X' if removed else 'O', key_text(source), model, RELATIONS[model][relation], key_text(target), checkpoint))
    out.append(HEADERS[6] + '\n')
    out += [line('decision', 'O', row['kind'], row['checkpoint'], row['description']) for row in decisions]
    out.append(HEADERS[7] + '\n')
    out += [line('body', 'O', key_text(key), body) for key, body in current_bodies(directory, keys)]
    with open(output, 'wb') as stream:
        stream.write(''.join(out).encode('utf-8'))


# 지우지 않은 항목의 마지막 버전 본문을 key 순서로
def current_bodies(directory, keys):
    latest = {}
    for row in load(directory, 'item_version'):
        if row['item_key'] not in latest or latest[row['item_key']]['checkpoint'] < row['checkpoint']:
            latest[row['item_key']] = row
    return sorted((keys[old_key], row['body']) for old_key, row in latest.items() if not row['removed'] and row['body'])


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    directory, output = sys.argv[1], sys.argv[2]
    if not os.path.isfile(os.path.join(directory, 'item.jsonl')):
        sys.exit('item.jsonl 이 없음: 스키마 3 jsonl 이 아니다. ' + __doc__)
    write_kisp(directory, output)


if __name__ == '__main__':
    main()
