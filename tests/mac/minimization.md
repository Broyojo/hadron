# Minimization checks

These preserve the native AppKit reproducer and Win32 lifecycle checks behind
[finding #30](../../docs/findings.md). They need an interactive macOS desktop and
temporarily change focus. Do not run them during another Wine/game session.
They do not capture the screen or establish visual/gameplay acceptance.

## Native ordering reproducer

From the repository root:

```sh
python3 -B tests/mac/test-minimization.py --native-only
```

Or build and run the native program directly:

```sh
mkdir -p build/minimization-tests
xcrun clang -fobjc-arc -O2 -Wall -Wextra -Werror -framework AppKit \
  tests/mac/minimize-ordering.m -o build/minimization-tests/minimize-ordering
"$PWD/build/minimization-tests/minimize-ordering"
```

The program launches its own focus helper, makes its panel inactive, and requests
minimization. Twelve named cases preserve the original paired experiments:
immediate front/back ordering, ordering from `windowWillMiniaturize`, level
lowering, and style changes with and without a title bar. Duplicate repetitions
from the original probes share one harness and one set of controls.

Each case observes callbacks and native state **before** bringing focus back.
Immediate front/back cases are expected to prevent initiation; their matched
controls and ordering from `windowWillMiniaturize` are expected to complete.
Restoration is checked afterward. A pass means this competing-ordering behavior
was reproduced on the current desktop; it does not prove the behavior is identical
on other macOS versions, or test Wine's fix. Different behavior is reported as a
failure with the observed state, rather than silently accepted.

The program terminates its own helper, requests activation of the previously
foreground application, and exits within 45 seconds. Exit 0 requires all twelve
cases; mismatches exit 1 and setup/timeouts exit 2. Avoid interacting with the
test windows while it runs.

## Win32 lifecycle checks

Use a private, writable copy of a complete compatible Hadron runtime, as described
in CONTRIBUTING.md. Leave the runtime selected by Steam alone. The runner creates
a fresh prefix and support directory for every invocation; no game prefix is used.
Its use of `scripts/play` retains the watchdog. No Steam bridge is enabled.

```sh
python3 -B tests/mac/test-minimization.py \
  --runtime /path/to/private/runtime \
  --llvm-bin "$(brew --prefix llvm)/bin"
```

The prerequisites are Xcode's command-line tools, Python 3, LLVM (`clang`,
`llvm-dlltool`, and `lld-link`), and the fetched Wine headers under `src/wine`.
When LLVM is already on PATH, `--llvm-bin` can be omitted. The runner builds the
x86-64 Windows executable using Wine's headers and small generated import
libraries; no Windows SDK or CRT is required.

The executable preserves eleven checks: hidden/minimized creation, deferred show
and restore, ordinary show/minimize, hide/reshow while minimized, restore after
reshow, and cancellation of a minimized creation before showing. Each observation
reports expected and actual minimized/visible state, client dimensions, and
`GetClientRect` errors. A visible restored window must have a nonempty client.
The settling/message-pump delays are for these lifecycle observations; **they do
not establish restoration before native minimization starts**.

The runner prints a fresh directory under `build/minimization-tests/` containing
native and Wine logs, `results.txt`, generated binaries, prefix/support data, and
a receipt with executable/driver hashes and loaded-driver verification. It checks
the exact eleven phase names and successful summary as well as the launcher exit;
missing or partial output cannot pass. Wine execution is bounded to 60 seconds,
and cleanup targets only that run's private wineserver. Results are retained for
inspection, including failures.

These are small desktop experiments, not automatic CI tests. Graphics conformance,
other architectures, other macOS versions, and game rendering/input are separate
checks. The local instrumented restore-timing investigation is described in
finding #30; instrumentation is not part of the production patch queue.
