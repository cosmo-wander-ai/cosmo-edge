"""Decode frozen images to original-size lossless RGB PNG with an audit manifest."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re

from PIL import Image, __version__ as pillow_version

from .runner import read_manifest


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonicalize(manifest, output):
    rows = read_manifest(manifest)
    output = Path(output)
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    requests, audit = [], []
    for row in rows:
        identifier = row['id']
        if not isinstance(identifier, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', identifier):
            raise ValueError('unsafe sample id')
        source = Path(row['image']).read_bytes()
        source_hash = digest(source)
        if row.get('image_sha256') != source_hash:
            raise ValueError('missing or mismatched source image hash')
        with Image.open(io.BytesIO(source)) as image:
            image.seek(0)
            rgb = image.convert('RGB')
        target = output / (identifier + '.png')
        # No resize, orientation correction or color-profile transformation.
        rgb.save(target, format='PNG')
        target.chmod(0o600)
        png_hash = digest(target.read_bytes())
        with Image.open(target) as decoded:
            if decoded.mode != 'RGB' or decoded.size != rgb.size or decoded.tobytes() != rgb.tobytes():
                raise ValueError('lossless PNG roundtrip mismatch')
        requests.append({'id': identifier, 'image': str(target.resolve()), 'image_sha256': png_hash})
        audit.append({'id': identifier, 'source_image_sha256': source_hash, 'png_sha256': png_hash,
                      'decoded_rgb_sha256': digest(rgb.tobytes()), 'width': rgb.width, 'height': rgb.height})
    for name, values in (('requests.jsonl', requests), ('conversion.jsonl', audit)):
        path = output / name
        path.write_text(''.join(json.dumps(row) + '\n' for row in values))
        path.chmod(0o600)
    summary = {'source_manifest_sha256': digest(Path(manifest).read_bytes()), 'count': len(rows),
               'pillow_version': pillow_version, 'decode': 'first frame to RGB; no EXIF/ICC transform',
               'resize': False, 'lossless_roundtrip_verified': True,
               'ids_and_order_preserved': True, 'truth_or_split_changed': False,
               'files': {name: digest((output / name).read_bytes())
                         for name in ('requests.jsonl', 'conversion.jsonl')}}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (output / 'summary.json').chmod(0o600)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output', required=True, help='New private output directory')
    args = parser.parse_args()
    print(json.dumps(canonicalize(args.manifest, args.output), indent=2))


if __name__ == '__main__':
    main()
