#!/usr/bin/env python3
from pathlib import Path
import datetime
import argparse
import plistlib
import re
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description='Isolated Win32/native launch-focus checks; temporarily changes desktop focus.')
parser.add_argument('--runtime', required=True, type=Path)
parser.add_argument('--llvm-bin', type=Path, default=Path('/opt/homebrew/opt/llvm/bin'))
parser.add_argument('--case', choices=['inactive-fullscreen', 'inactive-windowed-topmost',
                                    'active-fullscreen', 'switch-during-delayed-creation',
                                    'fullscreen-transition', 'inactive-nontopmost'], help='run one control for a matched diagnostic comparison')
args = parser.parse_args()
runtime = args.runtime.resolve()
base = root / 'build/launch-focus'
base.mkdir(parents=True, exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='run-', dir=base))
print('Results:', out, flush=True)
driver = runtime / 'dist/lib/wine/aarch64-unix/winemac.so'
prefix = out / 'prefix'
support = out / 'support'
appid = '4294967002'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
receipt = {'out': str(out), 'runtime': str(runtime), 'prefix': str(prefix),
           'driver_sha256': sha(driver), 'cases': []}

# Compile the actual Win32 sequence and a native foreground parent/control.
def run(command):
    return subprocess.run([str(x) for x in command], check=True, timeout=60)
imports = {
 'kernel32': 'CreateFileA WriteFile FlushFileBuffers CloseHandle GetTickCount Sleep ExitProcess GetModuleHandleW GetCurrentProcessId GetEnvironmentVariableA',
 'user32': 'RegisterClassW CreateWindowExW DefWindowProcW DestroyWindow ShowWindow SetForegroundWindow SetWindowPos PeekMessageW TranslateMessage DispatchMessageW GetForegroundWindow GetActiveWindow GetFocus IsIconic IsWindowVisible wsprintfA GetSystemMetrics',
}
for library, symbols in imports.items():
    definition = out / (library + '.def')
    definition.write_text('LIBRARY ' + library + '.dll\nEXPORTS\n' + '\n'.join(symbols.split()) + '\n')
    run([args.llvm_bin / 'llvm-dlltool', '-m', 'i386:x86-64', '-d', definition, '-l', out / (library + '.lib')])
binary = out / 'launch-window.exe'
run([args.llvm_bin / 'clang', '--target=x86_64-windows', '-fuse-ld=lld', '-fno-builtin', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Werror',
     '-I', root / 'src/wine/include', '-I', root / 'src/wine/include/msvcrt', root / 'tests/win/launch-focus.c',
     out / 'kernel32.lib', out / 'user32.lib', '-Wl,/entry:start', '-Wl,/subsystem:console', '-o', binary])
probe = out / 'probe'
observer = out / 'observer'
launcher = out / 'launcher'
for source, target in [('launch-focus-parent.m', probe), ('launch-focus-observer.m', observer),
                       ('launch-focus-launcher.m', launcher)]:
    run(['xcrun', 'clang', '-fobjc-arc', '-O2', '-Wall', '-Wextra', '-Werror', '-framework', 'AppKit', '-framework', 'ApplicationServices',
         root / 'tests/mac' / source, '-o', target])
run(['codesign', '-f', '-s', '-', probe])
native = out / 'native'
native_prefix = 'com.hadron.launch-focus.' + out.name.replace('_', '-')
for role in ('Parent', 'Helper'):
    contents = native / (role + '.app') / 'Contents'
    (contents / 'MacOS').mkdir(parents=True)
    shutil.copy2(probe, contents / 'MacOS/probe')
    (contents / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'probe',
        'CFBundleIdentifier': native_prefix + '.' + role.lower(),
        'CFBundleName': role, 'CFBundlePackageType': 'APPL'}))
receipt.update(exe_sha256=sha(binary), parent_sha256=sha(probe), observer_sha256=sha(observer),
               launcher_sha256=sha(launcher))

def save():
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

def idle():
    for line in subprocess.check_output(['ps', '-axo', 'comm='], text=True).splitlines():
        name = line.strip().lower()
        if name.endswith('.exe') or Path(name).name in ('wine', 'wineserver', 'wine64'):
            raise RuntimeError('Existing Wine session; postpone control')

def stop_private():
    result = subprocess.run([str(runtime / 'dist/bin/wineserver'), '-k'], env=env,
                            capture_output=True, text=True, timeout=10)
    if result.returncode not in (0, 1) or (result.returncode == 1 and result.stderr):
        raise RuntimeError('Private wineserver cleanup failed: ' + result.stderr)
    subprocess.run([str(runtime / 'dist/bin/wineserver'), '-w'], env=env, check=True, timeout=10)

env = dict(os.environ)
for key in list(env):
    if key.startswith(('WINE', 'STEAM_', 'Steam', 'HADRON_', 'DYLD_', 'FEX_')):
        del env[key]
env.update(WINEPREFIX=str(prefix), HADRON_SUPPORT=str(support), HADRON_DISABLE_LSTEAMCLIENT='1',
           HADRON_WATCHDOG='1', HADRON_LOG='', HADRON_D3D12='wine', HADRON_D3D9='wined3d',
           HADRON_APP_NAME='launch-window', WINEDEBUG='-all,+timestamp,+pid,err+all,trace+loaddll')
watcher = None
try:
    idle()
    with (out / 'bootstrap.log').open('w') as log:
        subprocess.run([str(runtime / 'scripts/play'), appid, str(binary)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    stop_private()
    idle()
    with (out / 'desktop.jsonl').open('w') as log:
        watcher = subprocess.Popen([str(observer), '180'], stdout=log)
    for label, full, top, mode, delay, switch, expected in [
        ('inactive-fullscreen', 1, 1, 'wine-none', 0, 0, 0),
        ('inactive-windowed-topmost', 0, 1, 'wine-none', 0, 0, 21),
        ('active-fullscreen', 1, 1, 'wine-bundle', 0, 0, 27),
        ('switch-during-delayed-creation', 1, 1, 'wine-bundle', 1.2, 1, 0),
        ('fullscreen-transition', 1, 1, 'wine-bundle', 1.2, 1, 0),
        ('inactive-nontopmost', 1, 0, 'wine-none', 0, 0, 0)]:
        if args.case and label != args.case:
            continue
        idle()
        case = out / label
        case.mkdir()
        shutil.copytree(native / 'Parent.app', case / 'Parent.app')
        shutil.copytree(native / 'Helper.app', case / 'Helper.app')
        case_env = {key: value for key, value in env.items() if key.startswith(('WINE', 'HADRON_'))}
        if full: case_env['WINE_LAUNCH_PROBE_FULLSCREEN'] = '1'
        if top: case_env['WINE_LAUNCH_PROBE_TOPMOST'] = '1'
        if label == 'fullscreen-transition': case_env['WINE_LAUNCH_PROBE_TRANSITION'] = '1'
        if delay:
            case_env['WINE_LAUNCH_PROBE_DELAY'] = '1'
        script = '#!/bin/sh\n' + '\n'.join('export ' + key + '=' + shlex.quote(value)
            for key, value in case_env.items()) + '\n'
        script += 'exec ' + ' '.join(shlex.quote(str(value)) for value in
            [runtime / 'scripts/play', appid, binary]) + ' >' + shlex.quote(str(case / 'wine.log')) + ' 2>&1\n'
        (case / 'run-wine.sh').write_text(script)
        (case / 'run-wine.sh').chmod(0o755)
        parent = subprocess.Popen([str(launcher), str(case / 'Parent.app'),
                                   'parent', str(case), mode, str(delay), str(switch)])
        identities = []
        deadline = time.monotonic() + 15
        while parent.poll() is None:
            if time.monotonic() > deadline:
                raise RuntimeError('Parent control timeout')
            if not identities:
                for line in subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True).splitlines():
                    parts = line.strip().split(None, 1)
                    if len(parts) == 2 and 'launch-window.exe' in parts[1]:
                        files = subprocess.run(['lsof', '-p', parts[0], '-Fn'], capture_output=True, text=True).stdout
                        if str(driver) in files and str(prefix) in files:
                            (case / 'loaded-files.txt').write_text(files)
                            identities.append({'pid': int(parts[0]), 'comm': parts[1], 'driver_sha256': sha(driver)})
            time.sleep(.1)
        if parent.returncode:
            raise RuntimeError('Native parent exited with an error')
        stop_private()
        shutil.copy2(out / 'win32-launch.txt', case / 'win32-launch.txt')
        win32 = (case / 'win32-launch.txt').read_text()
        complete = all(phase in re.findall(r'phase=(\S+)', win32) for phase in
                       ('shown', 'foreground-requested', 'settled', 'before-close'))
        parent_rows = [json.loads(line) for line in (case / 'parent.jsonl').read_text().splitlines()]
        before = next(r for r in parent_rows if r['phase'] == 'before-spawn')
        main_pid = identities[0]['pid'] if identities else -1
        desktop = [json.loads(line) for line in (out / 'desktop.jsonl').read_text().splitlines()]
        snapshots = [r for r in desktop if r['event'] == 'state' and
                     any(w['pid'] == main_pid and w['onscreen'] for w in r['windows'])]
        snapshot = snapshots[-1] if snapshots else {}
        windows = [w for w in snapshot.get('windows', []) if w['pid'] == main_pid and w['onscreen']]
        main_window = max(windows, key=lambda w:w['bounds']['Width']*w['bounds']['Height']) if windows else {}
        expected_active = mode == 'wine-bundle' and not switch
        active = snapshot.get('front', {}).get('pid') == main_pid
        front = snapshot.get('front', {}).get('bundle')
        expected_front = 'com.broyojo.hadron.game.4294967002' if expected_active else native_prefix + '.' + ('helper' if switch else 'parent')
        qualified = before['active'] and front == expected_front and bool(identities)
        passed = complete and qualified and main_window.get('layer') == expected and active == expected_active
        front_windows = [w for w in snapshot.get('windows', []) if w['pid'] == snapshot.get('front', {}).get('pid') and
                         w['onscreen'] and w['layer'] == 0 and w['bounds']['Height'] > 30]
        behind = bool(front_windows) and bool(main_window) and main_window['global_rank'] > min(w['global_rank'] for w in front_windows)
        require_behind = bool(full and not expected_active)
        if require_behind:
            passed = passed and behind
        row = {'label': label, 'delay': delay, 'switch': bool(switch), 'native_active': active,
               'expected_level': expected, 'window': main_window, 'front': snapshot.get('front'),
               'behind_front_window': behind, 'require_behind': require_behind,
               'qualified': qualified, 'loaded_identities': identities, 'result': 'PASS' if passed else 'FAIL'}
        receipt['cases'].append(row)
        save()
        print(json.dumps(row), flush=True)
        if not passed: raise RuntimeError('Native state/order mismatch or unqualified desktop; inspect logs')
finally:
    if watcher is not None:
        watcher.terminate()
        watcher.wait(timeout=10)
    stop_private()
    receipt['driver_unchanged'] = sha(driver) == receipt['driver_sha256']
    receipt['ended_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
    print(json.dumps({'out': str(out), 'driver_unchanged': receipt['driver_unchanged']}), flush=True)
    if not receipt['driver_unchanged']:
        raise RuntimeError('Runtime driver changed during the checks')
