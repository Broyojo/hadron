#!/usr/bin/env python3
"""Run the small AppKit ordering reproducer and eleven Win32 lifecycle checks.

Use --native-only, or --runtime <private-runtime> --llvm-bin <LLVM bin directory>.
An interactive macOS desktop is required; these tests temporarily change focus.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-only', action='store_true')
parser.add_argument('--runtime', type=Path)
parser.add_argument('--llvm-bin', type=Path, help='directory containing clang and llvm-dlltool')
args = parser.parse_args()
if not args.native_only and not args.runtime:
    parser.error('--runtime is required unless --native-only is used')
base = root / 'build/minimization-tests'
base.mkdir(parents=True, exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='run-', dir=base))
print('Results:', out, flush=True)

def run(command, **options):
    return subprocess.run([str(item) for item in command], check=True, timeout=60, **options)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

native = out / 'minimize-ordering'
run(['xcrun', 'clang', '-fobjc-arc', '-O2', '-Wall', '-Wextra', '-Werror',
     '-framework', 'AppKit', root / 'tests/mac/minimize-ordering.m', '-o', native])
with (out / 'native.log').open('w') as log:
    run([native], stdout=log, stderr=subprocess.STDOUT)
native_text = (out / 'native.log').read_text()
if ('SUMMARY cases=12 expected=12 failures=0 exit=0' not in native_text or
        len(re.findall(r'phase=restored result=PASS', native_text)) != 12):
    raise RuntimeError('Incomplete native results; inspect native.log')
print('PASS native ordering: 12 cases', flush=True)
if args.native_only:
    raise SystemExit(0)

runtime = args.runtime.resolve()
driver = runtime / 'dist/lib/wine/aarch64-unix/winemac.so'
for path in [runtime / 'scripts/play', runtime / 'dist/bin/wineserver', driver]:
    if not path.is_file():
        raise RuntimeError('Missing runtime file: ' + str(path))

# Use Wine's Windows headers and small import libraries; no Windows CRT or SDK.
llvm = args.llvm_bin.resolve() if args.llvm_bin else None
clang = llvm / 'clang' if llvm else shutil.which('clang')
dlltool = llvm / 'llvm-dlltool' if llvm else shutil.which('llvm-dlltool')
if not clang or not dlltool:
    raise RuntimeError('LLVM clang and llvm-dlltool are required; supply --llvm-bin')
imports = {
    'kernel32': 'CreateFileA WriteFile FlushFileBuffers CloseHandle GetTickCount Sleep '
                'ExitProcess GetModuleHandleW GetLastError',
    'user32': 'RegisterClassW CreateWindowExW DefWindowProcW DestroyWindow ShowWindow '
              'PeekMessageW TranslateMessage DispatchMessageW GetClientRect IsIconic '
              'IsWindowVisible wsprintfA',
}
for library, symbols in imports.items():
    definition = out / (library + '.def')
    definition.write_text('LIBRARY ' + library + '.dll\nEXPORTS\n' +
                          '\n'.join(symbols.split()) + '\n')
    run([dlltool, '-m', 'i386:x86-64', '-d', definition, '-l', out / (library + '.lib')])
exe = out / 'minimize-lifecycle.exe'
run([clang, '--target=x86_64-windows', '-fuse-ld=lld', '-fno-builtin', '-nostdlib',
     '-O2', '-Wall', '-Wextra', '-Werror', '-I', root / 'src/wine/include',
     '-I', root / 'src/wine/include/msvcrt',
     root / 'tests/win/minimize-lifecycle.c', out / 'kernel32.lib', out / 'user32.lib',
     '-Wl,/entry:start', '-Wl,/subsystem:console', '-o', exe])

def wine_processes():
    rows = []
    for line in subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True).splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        name = Path(parts[1]).name.lower()
        if (name in ('wine', 'wine64', 'wineserver', 'wine-preloader', 'wine64-preloader') or
                name.endswith('.exe') or '/wine.app/' in parts[1]):
            rows.append((int(parts[0]), parts[1]))
    return rows

# The watchdog can stop all Hadron processes; avoid overlapping another session.
if wine_processes():
    raise RuntimeError('Existing Wine process; run after that session has ended')
prefix = out / 'prefix'
env = os.environ.copy()
for key in list(env):
    if key.startswith(('WINE', 'STEAM_', 'Steam', 'HADRON_', 'DYLD_', 'FEX_')):
        del env[key]
env.update(WINEPREFIX=str(prefix), HADRON_SUPPORT=str(out / 'support'),
           HADRON_DISABLE_LSTEAMCLIENT='1', HADRON_WATCHDOG='1', HADRON_LOG='',
           HADRON_D3D12='wine', HADRON_D3D9='wined3d',
           WINEDEBUG='-all,+timestamp,+pid,err+all,trace+loaddll')
receipt = {'runtime': str(runtime), 'prefix': str(prefix),
           'driver_sha256': sha(driver), 'exe_sha256': sha(exe),
           'native_sha256': sha(native), 'loaded_driver_verified': False}
process = None
try:
    with (out / 'wine.log').open('w') as log:
        process = subprocess.Popen([str(runtime / 'scripts/play'), '4294967001', str(exe)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while process.poll() is None:
            if time.monotonic() > deadline:
                raise RuntimeError('Wine test timed out after 60 seconds')
            if not receipt['loaded_driver_verified']:
                for pid, command in wine_processes():
                    if 'minimize-lifecycle' not in command:
                        continue
                    files = subprocess.run(['lsof', '-p', str(pid), '-Fn'],
                                           capture_output=True, text=True, check=True).stdout
                    if str(driver) in files and str(prefix) in files:
                        (out / 'loaded-files.txt').write_text(files)
                        receipt.update(loaded_driver_verified=True, test_pid=pid)
                        break
            time.sleep(0.1)
        receipt['launcher_exit'] = process.returncode
        if process.returncode:
            raise RuntimeError('Wine launcher failed; inspect wine.log')
    text = (out / 'results.txt').read_text()
    expected = [
        'created-hidden-minimized', 'shown-still-minimized', 'restored-after-deferred',
        'ordinary-visible', 'ordinary-minimized', 'hidden-while-minimized',
        'reshown-still-minimized', 'restored-after-reshow', 'cancel-created-hidden-minimized',
        'cancel-restored-directly', 'cancel-remains-restored',
    ]
    phases = re.findall(r'^phase=(\S+).*result=PASS$', text, re.MULTILINE)
    if phases != expected or 'SUMMARY checks=11 expected=11 failures=0' not in text:
        raise RuntimeError('Incomplete or failing lifecycle results; inspect results.txt')
    if not receipt['loaded_driver_verified']:
        raise RuntimeError('Loaded driver identity was not verified')
finally:
    # This newly created prefix belongs only to this run. Never use scripts/stop.
    try:
        if process is not None:
            subprocess.run([str(runtime / 'dist/bin/wineserver'), '-k'], env=env,
                           capture_output=True, check=True, timeout=10)
            process.wait(timeout=10)
            run([runtime / 'dist/bin/wineserver', '-w'], env=env)
        receipt['driver_unchanged'] = sha(driver) == receipt['driver_sha256']
        if not receipt['driver_unchanged']:
            raise RuntimeError('Runtime driver changed during the test')
    except Exception as error:
        receipt['cleanup_error'] = str(error)
        raise
    finally:
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('PASS Win32 lifecycle: 11 checks, loaded driver verified', flush=True)
