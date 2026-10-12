#!/usr/bin/env python3
"""Exercise the real spawn hook and steam-run route with private, inert backends."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
subprocess.run(['make', '-C', str(root / 'src/notproton'), 'spawn-env'], check=True)
hook = root / 'src/notproton/out/spawn-env'
with tempfile.TemporaryDirectory(prefix='hadron-route-') as temporary:
    base = Path(temporary)
    runtime = base / 'runtime with spaces'
    scripts = runtime / 'scripts'
    scripts.mkdir(parents=True)
    for name in ('steam-run', 'paths.sh', 'runtime-versions.sh'):
        shutil.copy2(root / 'scripts' / name, scripts / name)
    # No BUILD-ID: runtime management cannot select/copy a real installation.
    events = base / 'events.jsonl'
    backend = base / 'backend'
    backend.write_text('#!' + shutil.which('python3') + '\n' + '''import json, os, sys
from pathlib import Path
with open(os.environ['ROUTE_EVENTS'], 'a') as out:
    out.write(json.dumps([Path(sys.argv[0]).name, sys.argv[1:]]) + '\\n')
''')
    backend.chmod(0o755)
    for relative in ('dist/bin/wine', 'dist/bin/wineserver', 'steam/activation-request',
                     'scripts/play', 'scripts/shortcut-icon'):
        target = runtime / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(backend, target)
    data = base / 'compatdata/620'
    prefix = data / 'pfx'
    prefix.mkdir(parents=True)
    for name in ('.hadron-prefix-ready', '.hadron-profile-v1', 'system.reg'):
        (prefix / name).touch()
    (prefix / '.update-timestamp').write_text('disable\n')
    tool = base / 'compatibilitytools.d/notproton/run'
    tool.parent.mkdir(parents=True)
    tool.write_text('#!/bin/sh\nexec ' + shlex.quote(str(scripts / 'steam-run')) + ' "$@"\n')
    tool.chmod(0o755)
    install = str(base / "Game's folder")
    program = install + '/Launcher.exe'
    helper = str(base / 'Steam/legacycompat/iscriptevaluator.exe')
    env = dict(os.environ, HADRON_SUPPORT=str(base / 'support'),
               STEAM_COMPAT_DATA_PATH=str(data), STEAM_COMPAT_INSTALL_PATH=install,
               STEAM_COMPAT_APP_ID='620', SteamAppId='264710', ROUTE_EVENTS=str(events))
    for key in ('BUILD_ID', 'HADRON_RUNTIME_CHOSEN', 'DYLD_INSERT_LIBRARIES'):
        env.pop(key, None)
    count = 0

    def check(args, route, app_id='620', overrides=None, form='direct'):
        global count
        events.write_text('')
        selected = env | (overrides or {})
        command = [str(tool), *args]
        if form == 'shell':
            command = ['/bin/sh', '-c', shlex.join(command)]
        elif form == 'split-quote':
            command = ['/bin/sh', '-c', shlex.quote(str(tool.parent) + '/.') + '/run ' + shlex.join(args)]
        elif form == 'positional':
            command = ['/bin/sh', '-c', 'exec "$@"', 'sh', *command]
        result = subprocess.run([str(hook), '--spawn', *command], env=selected,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        rows = [json.loads(line) for line in events.read_text().splitlines()]
        requests = [argv for name, argv in rows if name == 'activation-request']
        games = [argv for name, argv in rows if name == 'play']
        helpers = [argv for name, argv in rows if name == 'wine']
        if route == 'game':
            # Setting words are removed by steam-run, with all other quoting intact.
            command_args = [a for a in args[1:] if not a.startswith('HADRON_')]
            assert requests == [[app_id]], rows
            assert games == [[app_id, *command_args]], rows
            assert not helpers, rows
            assert next(i for i, r in enumerate(rows) if r[0] == 'activation-request') < next(i for i, r in enumerate(rows) if r[0] == 'play'), rows
        elif route == 'helper':
            assert not requests and not games and helpers == [args[1:]], rows
        else:
            assert not rows and result.stdout.strip() == str(data), (rows, result.stdout)
        count += 1
        print('PASS', form, route, args[0], flush=True)

    for form in ('direct', 'shell', 'split-quote', 'positional'):
        check(['waitforexitandrun', program, 'argument with spaces', "quote'and\"double"], 'game', form=form)
        check(['run', 'HADRON_LOG=1', program, '--launcher'], 'game', form=form)
        check(['getcompatpath'], 'query', form=form)
        check(['run', helper, 'legacycompat\\evaluatorscript_620.vdf'], 'helper', form=form)
        check(['run', str(base / 'redist/vcredist.exe'), '/quiet'], 'helper', form=form)
    check(['run', program], 'game', '264710', {'STEAM_COMPAT_APP_ID': ''})
    check(['run', program], 'game', overrides={'STEAM_COMPAT_INSTALL_PATH': install + '/'})
    check(['run', install + '-other/game.exe'], 'helper')
    # The checkout layout selects the built helper rather than the packaged one.
    (runtime / 'src/notproton').mkdir(parents=True)
    built = runtime / 'build/notproton/activation-request'
    built.parent.mkdir(parents=True)
    shutil.copy2(backend, built)
    (runtime / 'steam/activation-request').unlink()
    check(['run', program], 'game')
    print(f'PASS: {count} route checks; no Wine, game, or activation API was run.')
