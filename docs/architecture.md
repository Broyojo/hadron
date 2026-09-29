# Architecture

proton-apple is an open-source Proton-style compatibility tool for macOS on Apple
Silicon. It runs Windows x86/x86_64 games without Rosetta 2: Wine runs natively as
arm64, and FEX translates x86 code inside Wine's ARM64EC/WoW64 emulator interface.

## Stack

| Layer | Component | Notes |
|---|---|---|
| Wine | Upstream Wine master, arm64 macOS host | PE archs `aarch64,arm64ec,i386`. Proton and CrossOver game patches carried on top as needed. |
| x86_64 emulation | FEX `libarm64ecfex.dll` | Loaded as the ARM64EC emulator (`HKLM\Software\Microsoft\Wow64\amd64`). |
| i386 emulation | FEX `libwow64fex.dll` | Loaded as the WoW64 emulator (`HKLM\Software\Microsoft\Wow64\x86`). |
| D3D10/11 | DXMT | Direct to Metal, ARM64X builds. |
| D3D12 | vkd3d-proton on KosmicKrisp | Experimental; watch DXMT's D3D12 work. |
| D3D8/9 | mtld3d now, DXVK on KosmicKrisp later | DXVK 3.x needs geometry shaders, transform feedback and fillModeNonSolid. |
| Vulkan | KosmicKrisp (MoltenVK at first) | Via winevulkan. |
| Steam | NotProton's lsteamclient port, Steam Play in native Mac Steam | |
| Fixes | umu-protonfixes | |

Excluded: D3DMetal (non-commercial licence, x86_64 only), MetalSharp (PolyForm
Noncommercial).

## macOS constraints (measured on macOS 27.0, M2 Pro)

| Constraint | Result | Consequence |
|---|---|---|
| `__PAGEZERO` < 4GB on an arm64 executable | SIGKILL at exec | Wine's loader must keep the default 4GB pagezero (patch `wine/0001`). |
| Mapping below 4GB after start (`MAP_FIXED`, `mach_vm_allocate`, `MAP_32BIT`, even after unmapping pagezero) | Refused | Blocks `KUSER_SHARED_DATA` at `0x7ffe0000`, i386 WoW64 and low image bases. **Open problem.** |
| RWX memory (`mmap` or `mprotect`) | EACCES | Windows `PAGE_EXECUTE_READWRITE` can't be honoured directly. |
| RW → RX `mprotect`, then execute | Works (ad-hoc signed, no hardened runtime) | Page flipping between RW and RX is viable. |
| `MAP_JIT` | Works without entitlement when not hardened; not combinable with `MAP_FIXED`; address hints into a hole are honoured | |
| Toggling `pthread_jit_write_protect_np` inside a signal handler | Reverted on sigreturn | Fault-driven JIT toggling must happen outside signal context. |

### Executable memory plan

FEX never executes x86 guest pages natively; only its own JIT output runs. So Wine can
back Windows RWX requests with RW host pages and flip a host page to RX on an execute
fault (and back to RW on a write fault). This needs no FEX changes. Later, FEX can
toggle explicitly around code emission for speed.

## Repository layout

```
sources.conf          upstream repos and refs
patches/<component>/  git format-patch queues applied by scripts/fetch.sh
scripts/              setup-toolchain, fetch, build-wine, build-fex
src/ build/ dist/     checkouts, build trees, installed runtime (git-ignored)
```
