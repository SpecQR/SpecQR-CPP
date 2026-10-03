#!/usr/bin/env python3
"""Live exact differential tests against a pinned owner baseline and Nayuki.

Only development tools use Python/Node/Nayuki. C++ runtime remains standard-only.
A failed comparison exits nonzero and writes the first mismatch; no expected
output is updated automatically.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from protocol import generate, matrix_hash

ROOT = Path(__file__).resolve().parents[1]
BASELINE_COMMIT = '15ad15e5c770ea0e39072f8f88b2733018f02ffd'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapter', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--node', default='node')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts' / 'conformance.json')
    parser.add_argument('--skip-internal', action='store_true', help='Explicitly omit exhaustive internal matrices')
    args = parser.parse_args()
    started = time.perf_counter()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    counters = {'publicMatrices': 0, 'nayukiMatrices': 0, 'rawMatrices': 0, 'capacityCases': 0,
                'boundaryEstimates': 0, 'gfProducts': 0, 'reedSolomonDegrees': 0, 'malformedInputs': 0, 'structuredAppendSets': 0, 'structuredAppendMatrices': 0}
    source_files = sorted((ROOT / 'src').glob('**/*')) + sorted((ROOT / 'include').glob('**/*'))
    source_hashes = {str(p.relative_to(ROOT)): digest(p) for p in source_files if p.is_file()}
    baseline_revision = subprocess.check_output(['git', '-C', str(args.baseline), 'rev-parse', 'HEAD'], text=True).strip()
    if baseline_revision != BASELINE_COMMIT:
        raise RuntimeError(f'Expected baseline {BASELINE_COMMIT}, received {baseline_revision}')
    for kind in ['public'] + ([] if args.skip_internal else ['internal']):
        print(f'Generating {kind} reference cases...', flush=True)
        ref_file = args.output.parent / f'{kind}-reference.jsonl'
        command = [args.node, str(ROOT / 'tools' / 'reference.mjs'), str(args.baseline.resolve())]
        if kind == 'internal':
            command.append('--internal')
        with ref_file.open('w') as handle:
            subprocess.run(command, stdout=handle, check=True)
        with ref_file.open() as handle:
            while True:
                records = []
                for _ in range(48):
                    line = handle.readline()
                    if not line:
                        break
                    records.append(json.loads(line))
                if not records:
                    break
                for record, actual in zip(records, generate(args.adapter, [r['request'] for r in records])):
                    request, expected = record['request'], record['expected']
                    if 'matrix' in actual:
                        actual['matrixHash'] = matrix_hash(actual['matrix'])
                    if 'symbols' in actual:
                        actual['matrixHashes'] = [matrix_hash(s['matrix']) for s in actual['symbols']]
                        actual['versions'] = [s['version'] for s in actual['symbols']]
                        actual['masks'] = [s['mask'] for s in actual['symbols']]
                        counters['structuredAppendMatrices'] += len(actual['symbols'])
                    mismatches = {key: {'expected': value, 'actual': actual.get(key)}
                                  for key, value in expected.items() if actual.get(key) != value}
                    if mismatches or 'error' in actual:
                        failure = {'request': request, 'mismatches': mismatches, 'actual': actual}
                        failure_file = args.output.parent / 'first-conformance-failure.json'
                        failure_file.write_text(json.dumps(failure, indent=2, ensure_ascii=False) + '\n')
                        raise RuntimeError(f'Conformance mismatch; evidence: {failure_file.name}; {str(mismatches)[:700]}')
                    command = request.get('command')
                    counter = {'raw': 'rawMatrices', 'capacity': 'capacityCases', 'estimate': 'boundaryEstimates',
                               'structured-append': 'structuredAppendSets', 'rs': 'reedSolomonDegrees'}.get(command, 'publicMatrices')
                    if command == 'gf':
                        counters['gfProducts'] += 65536
                    else:
                        counters[counter] += 1
                    counters['nayukiMatrices'] += int(record.get('independent', False))
        print(f'PASS {kind}: {json.dumps(counters)}', flush=True)
    malformed = []
    for value in [b'\xc0\xaf', b'\xc1\xbf', b'\xc2', b'\xe0\x80\xaf', b'\xed\xa0\x80', b'\xf0\x80\x80\xaf',
                  b'\xf4\x90\x80\x80', b'\xf5\x80\x80\x80', b'\x80', b'\xff', b'A\xe2\x82', b'\xe2(\xa1']:
        for mode in ['auto', 'byte', 'kanji']:
            malformed.append({'rawText': list(value), 'options': {'mode': mode}})
    for options in [{'version': 0}, {'version': 41}, {'maskPattern': 8}, {'maskPattern': -2},
                    {'eci': -2}, {'eci': 1000000}, {'fnc1Second': '?'}, {'fnc1Second': 'AA'},
                    {'mode': 'numeric'}, {'mode': 'alphanumeric'}, {'mode': 'kanji'},
                    {'gs1': True, 'mode': 'alphanumeric'}]:
        malformed.append({'text': 'bad%🙂', 'options': options})
    for request, actual in zip(malformed, generate(args.adapter, malformed)):
        if 'error' not in actual or actual['error'] == 'ADAPTER':
            raise RuntimeError(f'Malformed input was not rejected by library: {request}, {actual}')
        counters['malformedInputs'] += 1
    if any(digest(ROOT / name) != value for name, value in source_hashes.items()):
        raise RuntimeError('Sources changed during the run; rerun against an immutable build')
    report = {'status': 'passed', 'baselineRepository': 'https://github.com/SpecQR/SpecQR',
              'baselineCommit': BASELINE_COMMIT, 'independentOracle': 'nayuki-qr-code-generator@1.8.0',
              'counts': counters, 'internalScopeExecuted': not args.skip_internal,
              'adapterSha256': digest(args.adapter), 'sourceSha256': source_hashes,
              'elapsedSeconds': round(time.perf_counter() - started, 3),
              'intentionalDifferences': ['High-level FNC1 literal percent compares with explicitly byte-encoded JS output.'],
              'limitations': ['Finite deterministic corpus, not an ISO certification or proof for all inputs.']}
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'sourceSha256'}, indent=2))


if __name__ == '__main__':
    main()
