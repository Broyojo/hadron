#!/usr/bin/env python3
"""macOS Wine allocation regression: native region safety and a bounded thread reproducer.

Run after building Wine: python3 -B tests/mac/test-wine-address-search.py
Use --runtime <runtime-root> to test an isolated runtime instead of the checkout's dist.
The test-only host reservations commit no memory; no game prefix is touched.
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', type=Path, default=root)
args = parser.parse_args()
runtime = args.runtime.resolve()
out = root / 'build/wine-address-search-tests'
out.mkdir(parents=True, exist_ok=True)

def run(command, **options):
    subprocess.run([str(item) for item in command], check=True, timeout=180, **options)

# Compile the actual helper from Wine rather than a separate copy of its implementation.
source = (root / 'src/wine/dlls/ntdll/unix/virtual.c').read_text()
start = source.index('static void *next_mmap_candidate(')
end = source.index('\n}\n', start) + 3
(out / 'wine-address-search.inc').write_text(source[start:end])
run(['/usr/bin/clang', '-O2', '-Wall', '-Wextra', '-I', out,
     root / 'tests/mac/wine-host-regions.c', '-o', out / 'host-region-tests'])
run([out / 'host-region-tests'])
run(['/usr/bin/clang', '-O2', '-Wall', '-Wextra', '-dynamiclib',
     root / 'tests/mac/wine-host-gaps.c', '-o', out / 'host-gap-probe.dylib'])

for arch, target in [('arm64', 'aarch64'), ('x64', 'x86_64')]:
    run([root / f'toolchains/llvm-mingw/bin/{target}-w64-mingw32-clang', '-O2',
         root / 'tests/win/thread-allocation.c', '-o', out / f'thread-{arch}.exe'])
    env = {
        'HOME': str(Path.home()), 'USER': os.environ['USER'], 'LANG': 'en_US.UTF-8',
        'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
        'WINEPREFIX': str(out / f'prefix-{arch}'), 'WINEDEBUG': '-all',
        'WINEDLLOVERRIDES': 'mscoree,mshtml=', 'DYLD_FALLBACK_LIBRARY_PATH': '/opt/homebrew/lib',
        'FEX_DISKCACHE': '1', 'WINE_VM_TRACE': '1', 'WINE_VM_TRACE_LOG': str(out / f'{arch}.vm.log'),
    }
    # Initialize before injecting host mappings; wineboot is outside the experiment.
    with (out / f'{arch}-wineboot.log').open('w') as log:
        run([runtime / 'dist/bin/wine', 'wineboot', '--init'], env=env,
            stdout=log, stderr=subprocess.STDOUT)
        run([runtime / 'dist/bin/wineserver', '-w'], env=env)
    env |= {'WINE_VM_INJECT_HOST_GAPS': '1',
            'DYLD_INSERT_LIBRARIES': str(out / 'host-gap-probe.dylib')}
    with (out / f'{arch}.stdout.log').open('w') as log:
        run([runtime / 'dist/bin/wine', out / f'thread-{arch}.exe', 'reserve'],
            env=env, stdout=log, stderr=subprocess.STDOUT)
        run([runtime / 'dist/bin/wineserver', '-w'], env=env)
    for line in (out / f'{arch}.stdout.log').read_text().splitlines():
        if line.startswith(('PASS', 'CreateThread', 'VirtualQuery')):
            print(arch + ': ' + line, flush=True)
print('PASS Wine host-address-search regression', flush=True)
