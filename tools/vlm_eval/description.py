"""Offline, hash-verified BGE-M3 dense scoring of preserved first attempts."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from .parser import parse_output
from .report import read_jsonl, sha256
from .scoring import score_results

GRADE_RULE = 'explicit-level-labels-v2; remove full labelled safety/risk level clauses, grade/level labels, bracketed Roman labels; accept I-IV, 1-4, one-four; retain unlabelled numbers and pronoun I'
_LEVEL = r'(?:IV|III|II|I|[1-4]|one|two|three|four)'
GRADE_PATTERN = re.compile(
    rf'\b(?:the\s+)?(?:safety|risk)\s*[_ -]?\s*level\s*(?:(?:is|was)\s*|[:=：-]\s*)?'
    rf'(?:(?:grade|level)\s*[:=：-]?\s*)?{_LEVEL}\b'
    rf'|\b(?:grade|level)\s*[:=：-]?\s*{_LEVEL}\b'
    r'|[\[(]\s*(?:IV|III|II|I)\s*[\])]', re.IGNORECASE)


def strip_grade(text):
    return ' '.join(GRADE_PATTERN.sub('', text).split()).strip(' .,:;-')


def verify_weights(directory):
    directory = Path(directory)
    path = directory / 'weights-manifest.json'
    manifest = json.loads(path.read_text())
    if manifest.get('model') != 'BAAI/bge-m3' or not re.fullmatch(r'[0-9a-f]{40}', manifest.get('revision', '')):
        raise ValueError('expected immutable BAAI/bge-m3 weight identity')
    found = set()
    for entry in manifest['files']:
        name = entry['name']
        if Path(name).name != name or name in found:
            raise ValueError('unsafe or duplicate model file')
        found.add(name)
        digest = hashlib.sha256()
        with (directory/name).open('rb') as stream:
            for block in iter(lambda: stream.read(8*1024*1024), b''):
                digest.update(block)
        if digest.hexdigest() != entry['sha256']:
            raise ValueError('model file hash mismatch: ' + name)
    if not {'config.json', 'pytorch_model.bin', 'tokenizer.json', 'tokenizer_config.json', 'special_tokens_map.json', 'sentencepiece.bpe.model'} <= found:
        raise ValueError('incomplete dense model manifest')
    return manifest, sha256(path)


class DenseEncoder:
    def __init__(self, directory, max_length=8192):
        import torch
        import transformers
        from transformers import AutoModel, AutoTokenizer
        self.torch = torch
        self.versions = dict(torch=torch.__version__, transformers=transformers.__version__)
        self.max_length = max_length
        self.tokenizer = AutoTokenizer.from_pretrained(directory, local_files_only=True, trust_remote_code=False)
        self.model = AutoModel.from_pretrained(directory, local_files_only=True, trust_remote_code=False).float().cpu().eval()

    def __call__(self, texts):
        inputs = self.tokenizer(texts, padding=True, truncation=True, max_length=self.max_length, return_tensors='pt')
        with self.torch.inference_mode():
            hidden = self.model(**inputs).last_hidden_state[:, 0]
            normalized = self.torch.nn.functional.normalize(hidden, p=2, dim=1)
        return normalized


def make_scores(truths, results, encoder):
    # Validate IDs and attempt uniqueness before computing any embeddings.
    score_results(truths, results)
    first = {row['id']: row for row in results if row.get('attempt', 1) == 1}
    scores = {}
    for truth in truths:
        key = truth['id']
        row = first.get(key)
        if row is None or row.get('status') != 'ok':
            continue
        parsed = parse_output(row.get('raw_output'))
        if not parsed['description_valid']:
            continue
        try:
            reference, predicted = strip_grade(truth['description']), strip_grade(parsed['description'])
            if not reference or not predicted:
                raise ValueError('empty description after grade stripping')
            embeddings = encoder([reference, predicted])
            value = float((embeddings[0] * embeddings[1]).sum())
            if not -1.00001 <= value <= 1.00001:
                raise ValueError('nonfinite or invalid cosine')
            scores[key] = dict(status='ok', cosine=max(-1.0, min(1.0, value)))
        except Exception as error:
            scores[key] = dict(status='scoring_error', error=type(error).__name__ + ': ' + str(error))
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('truth', 'results', 'model-dir', 'output'):
        parser.add_argument('--'+field, required=True)
    parser.add_argument('--max-length', type=int, default=8192)
    args = parser.parse_args()
    if not 1 <= args.max_length <= 8192:
        parser.error('max length must be 1..8192 and frozen before formal evaluation')
    manifest, manifest_hash = verify_weights(args.model_dir)
    encoder = DenseEncoder(args.model_dir, args.max_length)
    scores = make_scores(read_jsonl(args.truth), read_jsonl(args.results), encoder)
    result = dict(truth_sha256=sha256(args.truth), results_sha256=sha256(args.results),
                  scorer=dict(model='BAAI/bge-m3', revision=manifest['revision'], weights_sha256=manifest_hash,
                              encoding='AutoModel last_hidden_state[:,0], eval, CPU float32, batch two texts, no instruction',
                              normalization='L2 per embedding; dot product cosine',
                              truncation=dict(enabled=True, max_length=args.max_length, side=encoder.tokenizer.truncation_side),
                              grade_stripping=GRADE_RULE, versions=encoder.versions,
                              implementation_sha256=sha256(__file__)), scores=scores)
    with Path(args.output).open('x') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
    if any(score['status'] != 'ok' for score in scores.values()):
        raise SystemExit(2)


if __name__ == '__main__':
    main()
