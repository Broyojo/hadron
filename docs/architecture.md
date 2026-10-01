# Architecture

Hadron is an open-source Proton-style compatibility tool for macOS on Apple
Silicon. It runs Windows x86/x86_64 games without Rosetta 2: Wine runs natively as
arm64, and FEX translates x86 code inside Wine's ARM64EC/WoW64 emulator interface.

## Stack

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="architecture-dark.svg">
  <img alt="Hadron's stack, from Steam for Mac down to Metal" src="architecture-light.svg">
</picture>

The diagram is drawn by `tools/arch-diagram/gen.py`.

| Layer | Component | Notes |
|---|---|---|
| Wine | Upstream Wine master, arm64 macOS host | PE archs `aarch64,arm64ec,i386`. Proton and CrossOver game patches carried on top as needed. |
| x86_64 emulation | FEX `libarm64ecfex.dll` | Loaded as the ARM64EC emulator (`HKLM\Software\Microsoft\Wow64\amd64`). |
| i386 emulation | FEX `libwow64fex.dll` | Loaded as the WoW64 emulator (`HKLM\Software\Microsoft\Wow64\x86`). |
| D3D10/11 | DXMT | Direct to Metal, ARM64X builds. |
| D3D12 | vkd3d-proton on KosmicKrisp | The default for every game; presents through DXVK's DXGI. Watch DXMT's D3D12 work. |
| D3D8/9 | mtld3d now, DXVK on KosmicKrisp later | DXVK 3.x needs geometry shaders, transform feedback and fillModeNonSolid. |
| Vulkan | KosmicKrisp, Mesa's Vulkan driver on Metal 4 | Via winevulkan. Hadron's patches add what vkd3d-proton needs (docs/findings.md #22). |
| OpenGL | Wine's OpenGL on Apple's OpenGL 4.1 | Zink on KosmicKrisp planned, for OpenGL above 4.1. |
| Steam | NotProton's lsteamclient port, Steam Play in native Mac Steam | |
| Fixes | umu-protonfixes | |

Excluded: D3DMetal (non-commercial licence, x86_64 only), MetalSharp (PolyForm
Noncommercial).

## macOS constraints (measured on macOS 27.0, M2 Pro)

| Constraint | Result | Consequence |
|---|---|---|
| `__PAGEZERO` < 4GB on an arm64 executable | SIGKILL at exec | Wine's loader must keep the default 4GB pagezero (patch `wine/0001`). |
| Mapping below 4GB after start (`MAP_FIXED`, `mach_vm_allocate`, `MAP_32BIT`, even after unmapping pagezero) | Refused unless entitled (see below) | Blocks `KUSER_SHARED_DATA` at `0x7ffe0000`, i386 WoW64 and low image bases. |
| RWX memory (`mmap` or `mprotect`) | EACCES | Windows `PAGE_EXECUTE_READWRITE` can't be honoured directly. |
| RW → RX `mprotect`, then execute | Works (ad-hoc signed, no hardened runtime) | Page flipping between RW and RX is viable. |
| `MAP_JIT` | Works without entitlement when not hardened; not combinable with `MAP_FIXED`; address hints into a hole are honoured | |
| Toggling `pthread_jit_write_protect_np` inside a signal handler | Reverted on sigreturn | Fault-driven JIT toggling must happen outside signal context. |
| Mapping between 4GB and `0x7000000000` (hint, `MAP_FIXED`, `mach_vm_map` fixed) | Refused ("no space"); hints are moved to `0x7000000000` | Dev mode places `KUSER_SHARED_DATA` at `0x1007ffe0000` and starts the Windows address space at `0x7000000000`. |
| x18 across context switches | Zeroed by default; preserved with `os_set_custom_x18_abi_enabled(true)` (unrestricted `custom-x18-abi-toggle` entitlement, ad-hoc OK) | Every Wine thread opts in before entering Windows code (patch `wine/0004`). An on+off toggle pair costs ~19ns. |
| Custom x18 mode across signals | Restored to the pre-signal value on sigreturn | Signal handlers may switch it freely; returning to Windows code restores it. |

### The cross-architecture entitlement (macOS 26.4+)

Apple added kernel support for third-party x86 compatibility layers, gated on the
restricted entitlement `com.apple.developer.cross-architecture-support` (or
`...-unmanaged`). Found in XNU `xnu-12377.101.15`+ (`bsd/kern/mach_loader.c`,
`ml_satisfies_x86_64_requirements`) and in the shipping macOS 27 kernel. It grants:

| Feature | How |
|---|---|
| Low 4GB | "Soft" `__PAGEZERO`: one hard page, the rest a PROT_NONE mapping that can be replaced |
| 4K pages | `posix_spawnattr_set_4k_page_size_np()` (otherwise needs `com.apple.private.4k-pages`) |
| x18 as TEB | `os_set_custom_x18_abi_enabled()` (also allowed by unrestricted `com.apple.security.custom-x18-abi-toggle`) |
| `MAP_JIT` | Allowed |
| Per-thread x86 mode | `thread_set_x86_64_compat()` Mach trap (public in `mach_traps.h`, undocumented). Returns `KERN_FAILURE` unentitled. Likely hardware TSO; unverified. |

`os_cross_arch_is_supported(OS_CROSS_ARCH_X86_64)` (macOS 26.6+) reports kernel support;
it returns true on macOS 27. Ad-hoc signed binaries carrying the entitlement are killed
by AMFI ("adhoc signed but contains restricted entitlements"), so it must come from a
provisioning profile. In the developer portal it is the self-serve *Cross-architecture
Compatibility Framework* capability (see [apple-developer-setup.md](apple-developer-setup.md)).
This is almost certainly how CrossOver's ARM64 preview works, and explains its macOS
26.5 floor.

### Executable memory plan

FEX never executes x86 guest pages natively; only its own JIT output runs. So Wine can
back Windows RWX requests with RW host pages and flip a host page to RX on an execute
fault (and back to RW on a write fault). FEX's own code buffers are the exception: page
flipping made every compile or block link cost two faults (findings #14), so they are MAP_JIT
memory (Wine 0011) and FEX switches the thread to write mode around its code writes (FEX 0005).

## Repository layout

```
sources.conf          upstream repos and refs
patches/<component>/  git format-patch queues applied by scripts/fetch.sh
scripts/              build, install and launch scripts
tools/                test programs, Metal probes, benchmarks
src/ build/ dist/     checkouts, build trees, installed runtime (git-ignored)
```
