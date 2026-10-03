#!/usr/bin/env python3
"""Build/install into a disposable prefix; use only the exported package from another build."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', default=str(Path(__file__).resolve().parents[1]))
parser.add_argument('--config', default='Release', choices=['Debug', 'Release'])
parser.add_argument('--shared', action='store_true')
args = parser.parse_args()
source = Path(args.source).resolve()
cmake = os.environ.get('CMAKE', 'cmake')
def run(*command, **kwargs):
    subprocess.run([str(x) for x in command], check=True, **kwargs)
with tempfile.TemporaryDirectory(prefix='specqr-consumer-') as temporary:
    root = Path(temporary)
    build, prefix = root/'library-build', root/'prefix'
    run(cmake, '-S', source, '-B', build, f'-DCMAKE_BUILD_TYPE={args.config}',
        f'-DCMAKE_INSTALL_PREFIX={prefix}', '-DSPECQR_BUILD_TESTS=OFF',
        '-DSPECQR_BUILD_CLI=OFF', '-DSPECQR_BUILD_EXAMPLES=OFF',
        f'-DBUILD_SHARED_LIBS={"ON" if args.shared else "OFF"}')
    run(cmake, '--build', build, '--config', args.config, '--parallel', '2')
    run(cmake, '--install', build, '--config', args.config)
    # A relocated prefix with no library build tree must remain consumable.
    relocated = root/'relocated-prefix'
    prefix.rename(relocated)
    prefix = relocated
    shutil.rmtree(build)
    consumer = root/'consumer'
    shutil.copytree(source/'examples'/'consumer', consumer)
    consumer_build = root/'consumer-build'
    run(cmake, '-S', consumer, '-B', consumer_build,
        f'-DCMAKE_PREFIX_PATH={prefix}', f'-DCMAKE_BUILD_TYPE={args.config}')
    run(cmake, '--build', consumer_build, '--config', args.config, '--parallel', '2')
    candidates = [consumer_build/args.config/'consumer.exe', consumer_build/'consumer.exe',
                  consumer_build/args.config/'consumer', consumer_build/'consumer']
    executable = next(p for p in candidates if p.is_file())
    environment = dict(os.environ)
    if os.name == 'nt':
        environment['PATH'] = str(prefix/'bin') + os.pathsep + environment.get('PATH','')
    run(executable, env=environment, cwd=root)
print(f'clean installed consumer passed: {args.config}, shared={args.shared}')
