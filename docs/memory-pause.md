# Preserve a game at a watchdog limit

The watchdog suspends the affected Wine prefix with SIGSTOP instead of terminating
Hadron processes. The session's unsaved state stays in memory. It shows a dialog
explaining the pause; **Check and resume** requests another measurement. The session
continues only when none of the existing safety limits is exceeded. **Keep paused**
leaves it suspended. Run `scripts/resume` to request another check later.

Close other applications or free disk space before requesting a check. Suspension
prevents the paused processes from issuing further allocations, but does not release
their existing allocations or cancel GPU work already submitted. It cannot guarantee
that macOS will not terminate a process or become unresponsive. This is not a save
file, and does not survive a reboot. An explicit stop in Steam still ends the game.

Processes are selected by both their runtime mapping and exact Wine prefix, then
identified by PID and start time before every signal. Programs sharing a prefix
pause together: suspending only one side of Wine's IPC can leave the others blocked.
The guardian remains alive if the original launcher exits while a recorded process
is paused. Its record is next to the memory log (`.paused` and `.paused.guard`).
`resume` only requests a check from a guardian with the recorded identity. If that
guardian has died, it leaves the session intact and reports that manual recovery is
needed; automatic recovery after guardian failure is not implemented yet.

## Verification

On macOS, `tests/mac/test-watchdog-session.sh` builds a disposable sleeper and checks
suspension, continuation, isolation from another prefix (including paths containing
spaces), repeated suspension and stale process identities. No real game is signalled.
`tests/mac/test-watchdog-policy.sh` checks the unchanged thresholds and startup input
validation. Real-game suspension, GPU queues, audio and resume behaviour have not yet
been validated. Do not consider this ready for release until they have been tested.
