"""Build private scoring/request views from frozen, hash-verified inventories."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import tarfile


def read_jsonl(path):
    return [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]


def write_jsonl(path, rows):
    with path.open('x') as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False) + '\n')
    path.chmod(0o600)


def index_unique(rows, name):
    result = {}
    for row in rows:
        key = row['asset_id']
        if key in result:
            raise ValueError(f'duplicate {name} sample')
        result[key] = row
    return result


def prepare(args):
    if args.performance_count < 1:
        raise ValueError('performance count must be positive')
    output = Path(args.output)
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    full = read_jsonl(args.full)
    source = index_unique(read_jsonl(args.source_map), 'source')
    annotations = index_unique(read_jsonl(args.annotations), 'annotation')
    identifiers = [row['asset_id'] for row in full]
    if len(set(identifiers)) != len(identifiers):
        raise ValueError('duplicate frozen sample')
    calibration_groups = {row['group_id'] for row in full if row['assigned_to_calibration']}
    for row in full:
        if type(row['primary_heldout_eligible']) is not bool or type(row['assigned_to_calibration']) is not bool:
            raise ValueError('split membership must be Boolean')
        if row['primary_heldout_eligible'] and row['group_id'] in calibration_groups:
            raise ValueError('calibration source group leaks into independent split')
    truth, requests = [], []
    images = output / 'images'
    images.mkdir(mode=0o700)
    members = {}
    for row in full:
        identifier = row['asset_id']
        if not re.fullmatch(r'[A-Za-z0-9_-]+', identifier):
            raise ValueError('unsafe sample id')
        annotation = annotations[identifier]
        original = source[identifier]
        if annotation['level_candidate'] not in ('I', 'II', 'III', 'IV'):
            raise ValueError('invalid reference safety level')
        if not isinstance(annotation['reference_text'], str) or not annotation['reference_text'].strip():
            raise ValueError('empty reference description')
        if any(type(annotation['image'][axis]) is not int or annotation['image'][axis] < 1 for axis in ('width', 'height')):
            raise ValueError('invalid reference image dimensions')
        if original['sha256'] != row['image_sha256']:
            raise ValueError('inconsistent image identity')
        scene = Path(annotation['source_member_stem']).name.split('-')[0]
        truth.append({'id': identifier, 'safety_level': annotation['level_candidate'],
                      'description': annotation['reference_text'], 'scene': scene,
                      'group_id': row['group_id'],
                      'primary_heldout_eligible': row['primary_heldout_eligible'],
                      'assigned_to_calibration': row['assigned_to_calibration'],
                      'width': annotation['image']['width'], 'height': annotation['image']['height'],
                      'semantic_review': 'pending'})
        requests.append({'id': identifier, 'image': str((images / (identifier + '.jpg')).resolve()),
                         'image_sha256': row['image_sha256']})
        if original['member'] in members:
            raise ValueError('duplicate archive member')
        members[original['member']] = requests[-1]
    if args.extract:
        with tarfile.open(args.archive, 'r|gz') as archive:
            for member in archive:
                request = members.pop(member.name, None)
                if request is None:
                    continue
                if not member.isfile():
                    raise ValueError('image is not a regular archive member')
                data = archive.extractfile(member).read()
                if hashlib.sha256(data).hexdigest() != request['image_sha256']:
                    raise ValueError('image bytes hash mismatch')
                target = Path(request['image']); target.write_bytes(data); target.chmod(0o600)
        if members:
            raise ValueError('missing archive images')
    strata = defaultdict(list)
    for row in truth:
        size_bin = 'large' if row['width'] * row['height'] >= 1920 * 1080 else 'small'
        strata[(row['scene'], row['safety_level'], size_bin)].append(row['id'])
    for values in strata.values():
        values.sort(key=lambda identifier: hashlib.sha256(f'{args.seed}:{identifier}'.encode()).hexdigest())
    # Round-robin strata guarantee representation before proportionate exhaustion.
    selected = []
    while len(selected) < min(args.performance_count, len(truth)):
        for key in sorted(strata):
            if strata[key] and len(selected) < args.performance_count:
                selected.append(strata[key].pop(0))
    lookup = {row['id']: row for row in requests}
    write_jsonl(output / 'truth.full.jsonl', truth)
    write_jsonl(output / 'truth.independent.jsonl', [r for r in truth if r['primary_heldout_eligible']])
    write_jsonl(output / 'requests.full.jsonl', requests)
    independent_ids = {row['id'] for row in truth if row['primary_heldout_eligible']}
    write_jsonl(output / 'requests.independent.jsonl', [row for row in requests if row['id'] in independent_ids])
    write_jsonl(output / 'requests.performance.jsonl', [lookup[i] for i in selected])
    review_ids = []
    for key in sorted({(r['scene'],r['safety_level']) for r in truth}):
        candidates = [r['id'] for r in truth if (r['scene'],r['safety_level']) == key]
        candidates.sort(key=lambda i: hashlib.sha256(f'review:{args.seed}:{i}'.encode()).hexdigest())
        review_ids.extend(candidates[:2])
    write_jsonl(output / 'label-review.jsonl', [dict(r, review_status='pending', reviewer_notes='')
                                              for r in truth if r['id'] in review_ids or r['safety_level']=='III'])
    metadata = {'seed': args.seed, 'selection': 'deterministic round-robin scene/level/pixel-area strata',
                'full_count': len(truth), 'performance_count': len(selected), 'images_extracted': args.extract,
                'level_counts': dict(Counter(r['safety_level'] for r in truth)),
                'formal_evaluation_ready': False,
                'inputs': {name: hashlib.sha256(Path(getattr(args,name)).read_bytes()).hexdigest()
                           for name in ('full','source_map','annotations')},
                'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.jsonl')}}
    (output / 'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('full','source-map','annotations','archive','output'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--extract', action='store_true')
    parser.add_argument('--seed', type=int, default=20260917)
    parser.add_argument('--performance-count', type=int, default=100)
    args = parser.parse_args()
    if args.performance_count < 1:
        parser.error('performance count must be positive')
    prepare(args)


if __name__ == '__main__':
    main()
