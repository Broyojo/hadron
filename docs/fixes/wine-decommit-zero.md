# Partial host-page decommit must clear guest data

Wine remaps complete host pages when decommitting. With 16 KB host pages, boundary 4 KB guest
pages retained old data which became visible on recommit. Windows promises zeros for newly
committed pages, while committing an already committed page preserves data.

Patch 0034 zeros partial boundaries on macOS through temporary shared writable aliases,
leaving the original mapping's permissions unchanged. MAP_JIT pages use their existing
per-thread write mode because macOS refuses to alias them. It then clears commitment bits
and updates host protections from the remaining guest-page metadata. Complete host pages keep anonymous remapping.
Whole-view decommit also rounds the host end consistently for allocations smaller than a host
page. Cost: up to two alias map/protect/unmap cycles per unaligned decommit, plus boundary writes.
Both aliases are prepared before writing, so preparation failures leave guest data unchanged.
Other platforms retain their existing partial-boundary behaviour.

On an M4, macOS 27.0.1, the original release fails the independent test at offset 4096 with
0x5a rather than zero. The corrected runtime passes 17 cases on x86_64 and i386: single and
spanning ranges, whole small allocations, read-only protections, committed neighbours, and
preservation on repeated commit. The reduced Electron 2026.12 Node-mode startup changes from
an infinite V8 root-index probe to a verified numerical result; a one-million-iteration JIT
loop also completes. Normal launcher GUI stability is not established by these tests.

Build with llvm-mingw's x86_64/i686 compilers, then run through scripts/play with the watchdog.
This corrects memory semantics, not a game default or fabricated platform capability.
The initial runtime tests included unrelated experimental Wine/DXMT changes; the patch queue
is checked independently against current main and no dependency on those experiments is proposed.

## Independent PR build verification (2026-10-16)

Applied current main's Wine patches 0030–0033 to the prior pinned queue, then applied only
this patch as 0034. Built `ntdll.so` from that tree, without the experimental executable-ASLR
or foreign-swapchain source changes. Installed only this ntdll in the guarded diagnostic
runtime. Both x86_64 and i386 runs again pass all 17 cases (10:45 and 10:46).
The remaining runtime components were retained from the diagnostic copy; this verifies the
independent memory implementation, not a fully packaged current-main release.

## Review correction: committed-neighbour permissions

The review correctly identified a race in the first revision: temporarily changing a whole
host page to RW could permit writes to a committed read-only neighbour or remove execute
permission from concurrent code. `virtual_mutex` does not stop application threads.

A diagnostic build of that old revision inserted 20 ms after relaxing protections. The native
Windows ARM64 concurrent test then observed 83625271 successful protected writes and changed
neighbour 0x5a to 0xa5 (exit 10). The new alias-based path passes the same 100 decommit/recommit
cycles: zero successful protected writes, 394 caught access violations, neighbour unchanged.
No diagnostic delay is included in the patch. Initial try/except test variants did not catch
the intentionally generated faults; those failed harnesses are excluded. The committed probe
uses a single known ARM64 store and an explicit vectored handler that skips only that store.

The new ntdll also passes all 17 existing cases on x86_64 and i386. A native API contract probe
checks RO/RX alias writes while preserving source protection. MAP_JIT remapping returns a
protection error on this Mac, so those pages use per-thread write protection instead. The
separate native JIT probe executes neighbouring code 75166381 times with no incorrect result
while the current thread clears the other guest page. This checks the platform mechanism;
it is not a full Wine JIT-view integration test or a game acceptance run of this new revision.

Reproduction:

```sh
toolchains/llvm-mingw/bin/aarch64-w64-mingw32-clang -O2 -Wall -Wextra \
  tests/win/decommit-concurrent-arm64.c tests/win/decommit-store-arm64.S \
  -o build/decommit-concurrent-arm64.exe
HADRON_DISABLE_LSTEAMCLIENT=1 scripts/play decommit-protection build/decommit-concurrent-arm64.exe
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror tests/mac/jit-partial-clear.c -o build/jit-partial-clear
build/jit-partial-clear
```

Apple's API contract and implementation were consulted:
[mach_vm_remap](https://developer.apple.com/documentation/kernel/1402218-mach_vm_remap),
[XNU VM interfaces](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/vm/vm_user.c),
and the installed SDK's pthread/Mach declarations. No Metal/platform defect is claimed.

The ARM64 probe also has an `--execute` mode: neighbouring native code runs concurrently
from an executable/read-only host page while its other guest page is decommitted. The signed
Wine test passes with zero successful protected writes, 371 caught write faults, unchanged
neighbour bytes and zero wrong code results. This directly exercises executable ordinary
pages, beyond the separate native MAP_JIT mechanism check.

Final official queue check passes Wine (34), DXMT (3), and current-main NotProton (6).
The activation fixture was rerun against that current six-patch NotProton source and passes
all 24 routes. Other unchanged components were not fetched and are explicitly skipped.
