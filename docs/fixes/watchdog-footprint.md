# Memory footprint accounting and premature watchdog stops

## Failure and correction

M4, 16 GB RAM, macOS 27.0.1. Raft's world load was stopped at 8206 MB, just past an
intermediate half-RAM cap, with warning pressure and under 1 GB additional swap. The original
RSS accounting missed compressed/graphics allocations; the original 2 GB swap-growth cap
also stopped otherwise progressing loads.

`hadron-memory` sums `proc_pid_rusage` physical footprint, falling back per exited/inaccessible
process to its last RSS. No helper keeps the previous conservative hard limits. The new
automatic limits warn at half RAM, 2–4 GB swap growth, or warning pressure, once per session.
Stops occur at critical pressure, low disk, or (with elevated pressure) 90% RAM footprint or
half RAM swap growth, capped at 8 GB. Explicit user limits remain immediate hard limits.
Both runtime and home-volume free space are checked. The Steam runner owns the watchdog's
lifetime so a short-lived launcher cannot leave its child game unguarded.

## Verification

A measured Steam world load completed with peak 10878 MB footprint, 2021 MB swap growth,
warning pressure and one notification. The player subsequently confirmed Raft works after
cleanup, and confirmed Cities: Skylines through their explicit direct-entry preference.
Policy tests replay the recorded Raft loads and independent emergency/explicit/fallback cases.
The native helper tests live accounting, exited-process fallback and malformed arguments.
CI compiles the helper with warnings as errors and runs both tests.

No startup-speed or memory-allocation reduction is claimed. The policy gives progressing
loads more headroom; it cannot guarantee stability for every workload. The watchdog currently
accounts for all processes belonging to its runtime, as before. A full packaged-release game
run and a broader game matrix were not performed.
