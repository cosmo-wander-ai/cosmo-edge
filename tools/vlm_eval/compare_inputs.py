"""Compare device input dumps with frozen processor references, without inference."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compare(reference, dump_prefix):
    """Require exact tokens/grid and finite patches within absolute 2e-7."""
    pixel_path = Path(str(dump_prefix) + '.pixels.f32')
    token_path = Path(str(dump_prefix) + '.input.json')
    actual = json.loads(token_path.read_text())
    with np.load(reference, allow_pickle=False) as frozen:
        expected_ids = frozen['input_ids'].reshape(-1).tolist()
        expected_grid = frozen['image_grid_thw'].reshape(-1).tolist()
        expected_pixels = frozen['pixel_values'].reshape(-1)
        actual_pixels = np.fromfile(pixel_path, dtype='<f4')
        ids = actual.get('input_ids')
        grid = actual.get('grid_thw')
        tokens_equal = (isinstance(ids, list) and all(type(x) is int for x in ids)
                        and ids == expected_ids)
        grid_equal = (isinstance(grid, list) and all(type(x) is int for x in grid)
                      and grid == expected_grid)
        shape_equal = (pixel_path.stat().st_size == expected_pixels.size * 4
                       and actual_pixels.shape == expected_pixels.shape)
        finite = bool(np.isfinite(actual_pixels).all() and np.isfinite(expected_pixels).all())
        max_error = (float(np.max(np.abs(actual_pixels.astype(np.float64)
                                         - expected_pixels.astype(np.float64))))
                     if shape_equal and finite and expected_pixels.size else None)
    pixels_equal = max_error is not None and max_error <= 2e-7
    return {'passed': bool(tokens_equal and grid_equal and shape_equal and finite and pixels_equal),
            'tokens_exact': tokens_equal, 'grid_exact': grid_equal,
            'pixel_shape_equal': shape_equal, 'pixels_finite': finite,
            'pixel_max_abs_error': max_error, 'pixel_atol': 2e-7, 'pixel_rtol': 0,
            'expected_token_count': len(expected_ids),
            'expected_pixel_elements': int(expected_pixels.size),
            'actual_pixel_elements': int(actual_pixels.size),
            'reference_sha256': sha256(reference),
            'pixel_dump_sha256': sha256(pixel_path), 'token_dump_sha256': sha256(token_path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, help='Frozen reference .npz')
    parser.add_argument('--dump-prefix', required=True, help='Prefix of .pixels.f32 and .input.json')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = compare(args.reference, args.dump_prefix)
    with Path(args.output).open('x') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
