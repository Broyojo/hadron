# Partial host-page decommit must clear guest data

Wine remaps complete host pages when decommitting. With 16 KB host pages, boundary 4 KB guest
pages retained old data which became visible on recommit. Windows promises zeros for newly
committed pages, while committing an already committed page preserves data.

Patch 0034 zeros the partial boundaries through Wine's existing protection path, clears the
commitment bits, and restores host protections. Complete host pages keep anonymous remapping.
Whole-view decommit also rounds the host end consistently for allocations smaller than a host
page. Cost: writes/protection changes at up to two host pages per unaligned decommit.

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
