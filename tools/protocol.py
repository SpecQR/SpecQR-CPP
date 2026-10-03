"""Development-only bridge to the standard-library C++ test adapter."""
import hashlib
import json
from pathlib import Path
import subprocess


def request_line(request):
    command = request.get('command')
    if command == 'raw':
        return '\t'.join(map(str, ['raw', request['version'], request['ecc'], request['mask'], request['seed']]))
    if command == 'gf':
        return 'gf'
    if command == 'rs':
        return f"rs\t{request['degree']}"
    if command == 'structured-append':
        command = 'sa-segments' if 'segments' in request else 'sa-bytes' if 'bytes' in request else 'sa-text'
    if not command:
        command = 'segments' if 'segments' in request else 'bytes' if 'bytes' in request else 'text'
    options = request.get('options', {})
    sa = options.get('structuredAppend')
    fields = [command, options.get('version', -1), options.get('errorCorrectionLevel', 'M'),
              options.get('maskPattern', -1), options.get('mode', 'auto'),
              int(options.get('optimizeSegments', True)), int(options.get('boostErrorCorrection', False)),
              int(options.get('gs1', False)), options.get('eci', -1),
              options.get('fnc1Second', '').encode().hex(),
              ','.join(map(str, [sa['index'], sa['total'], sa['parity']])) if sa else '-',
              ','.join(map(str, [request.get('pngScale', 8 if request.get('png', False) else 0), options.get('minVersion', 1), options.get('maxVersion', 40), options.get('maxSymbols', 16)]))]
    if command in ['segments', 'sa-segments']:
        for segment in request['segments']:
            mode = segment['mode']
            if mode == 'eci':
                value = str(segment['assignmentNumber'])
            elif mode == 'fnc1':
                value = ''
            elif mode == 'fnc1-second':
                value = segment['applicationIndicator'].encode().hex()
            elif mode == 'structured-append':
                value = ','.join(map(str, [segment['index'], segment['total'], segment['parity']]))
            elif 'bytes' in segment:
                value = bytes(segment['bytes']).hex()
            else:
                value = segment.get('text', '').encode().hex()
            fields.append(('byte-text' if mode == 'byte' and 'text' in segment else mode) + ':' + value)
    elif 'rawText' in request:
        fields.append(bytes(request['rawText']).hex())
    elif 'bytes' in request:
        fields.append(bytes(request['bytes']).hex())
    else:
        fields.append(request.get('text', '').encode().hex())
    return '\t'.join(map(str, fields))


def generate(adapter, requests):
    process = subprocess.run([str(Path(adapter).resolve())], input='\n'.join(map(request_line, requests)) + '\n',
                             text=True, encoding='utf-8', capture_output=True)
    if process.returncode:
        raise RuntimeError(f'Adapter failed ({process.returncode}): {process.stderr[-4000:]}')
    results = [json.loads(line) for line in process.stdout.splitlines()]
    if len(results) != len(requests):
        raise RuntimeError('Test protocol response count differs from request count')
    return results


def matrix_hash(matrix):
    return hashlib.sha256(''.join(matrix).encode('ascii')).hexdigest()
