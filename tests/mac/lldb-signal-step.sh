#!/bin/bash
# Runs tests/mac/lldb-signal-step.c alone and under lldb, which passes SIGUSR2 on without stopping
# (as for a game) and continues past a breakpoint on tick() each time it is hit. Under lldb some
# self-signals are lost; alone, none are (findings #80).
# usage: tests/mac/lldb-signal-step.sh [seconds, default 5]
set -e
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
clang -O2 -o "$dir/lldb-signal-step" "$(dirname "$0")/lldb-signal-step.c"
echo "alone: $("$dir/lldb-signal-step" "${1:-5}" || true)"
cat > "$dir/drive.py" <<'PY'
import lldb
def drive(debugger, command, result, internal_dict):
    exe, secs = command.split()
    debugger.SetAsync(False)
    target = debugger.CreateTarget(exe)
    target.BreakpointCreateByName("tick")
    info = target.GetLaunchInfo()
    info.SetArguments([secs], False)
    error = lldb.SBError()
    process = target.Launch(info, error)
    debugger.HandleCommand("process handle SIGUSR2 -s false -p true -n false")
    while process.GetState() == lldb.eStateStopped:
        process.Continue()
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f drive.drive drive")
PY
echo "under lldb: $(xcrun lldb --batch -o "command script import $dir/drive.py" -o "drive $dir/lldb-signal-step ${1:-5}" 2>&1 | grep 'self-signals')"
