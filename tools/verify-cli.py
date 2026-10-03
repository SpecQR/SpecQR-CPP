#!/usr/bin/env python3
"""CLI end-to-end checks, including exact binary stdin on Windows."""
import argparse
from pathlib import Path
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--cli',required=True)
args=parser.parse_args()
cli=str(Path(args.cli).resolve())
checks=0
def run(arguments, data=None, status=0):
    global checks
    result=subprocess.run([cli,*arguments],input=data,capture_output=True)
    assert result.returncode==status,(arguments,result.returncode,result.stderr)
    checks+=1
    return result.stdout
with tempfile.TemporaryDirectory(prefix='specqr-cli-') as folder:
    folder=Path(folder)
    payload=bytes([0,10,13,10,26,127,128,255])+b'ABC%12'
    path=folder/'raw.bin';path.write_bytes(payload)
    a=run(['--binary','--stdin','--format','matrix'],payload)
    b=run(['--binary','--input',str(path),'--format','matrix'])
    assert a==b;checks+=1
    unicode_text='日本語😀'
    a=run(['--format','matrix',unicode_text])
    b=run(['--stdin','--format','matrix'],unicode_text.encode())
    assert a==b;checks+=1
    unicode_input=folder/'入力😀.txt'
    unicode_output=folder/'出力😀.txt'
    unicode_input.write_bytes(unicode_text.encode())
    run(['--input',str(unicode_input),'--format','matrix','--output',str(unicode_output)])
    assert unicode_output.read_bytes()==a;checks+=1
    a=run(['--stdin','--format','matrix'],b'Hello\r\nWorld')
    b=run(['--format','matrix','Hello\r\nWorld'])
    assert a==b;checks+=1
    run(['--help'])
    run(['--estimate','123456'])
    run(['--estimate','--version','1','1'*1000],status=3)
    for arguments in [[],['--wat'],['--input',''],['--output','','abc'],['--format','bad','abc'],
                      ['--ecc','X','abc'],['--version','1x','abc'],['--scale'],
                      ['--format','png','abc'],['one','two']]:
        run(arguments,status=2)
    run(['--format','matrix','--','--option-like-text'])
    out=folder/'image.png'
    run(['--format','png','--output',str(out),'SpecQR'])
    assert out.read_bytes().startswith(b'\x89PNG\r\n\x1a\n');checks+=1
    protected=folder/'unchanged';protected.write_text('original')
    run(['--format','png','--scale','0','--output',str(protected),'SpecQR'],status=2)
    assert protected.read_text()=='original';checks+=1
print(f'CLI: {checks} checks passed; 0 skipped')
