# proton-apple

An open-source, Proton-style compatibility tool for running Windows games on Apple
Silicon Macs without Rosetta: native arm64 Wine, FEX for x86 translation, DXMT and
Vulkan-on-Metal for graphics, and Steam Play integration in the native Mac Steam client.

Early development. See [docs/architecture.md](docs/architecture.md) for the stack and
the macOS constraints driving the design.

## Building

Requires an Apple Silicon Mac, Homebrew and the Xcode command line tools.

```sh
scripts/setup-toolchain.sh   # Homebrew deps + llvm-mingw
scripts/fetch.sh             # clone upstream sources and apply patches/
scripts/build-wine.sh        # native arm64 Wine -> dist/
scripts/build-fex.sh         # FEX emulator DLLs -> dist/
scripts/build-llvm15.sh      # static LLVM 15 for DXMT's shader compiler
scripts/build-dxmt.sh        # DXMT (D3D10/11 -> Metal) -> dist/, needs Xcode
```

## Status

- [x] Upstream Wine builds natively for arm64 macOS (aarch64 + arm64ec + i386 PE)
- [x] FEX ARM64EC and WoW64 DLLs build, with a Darwin unix helper
- [x] Native arm64 Windows programs run (dev build)
- [x] x86_64 Windows programs run through FEX, no Rosetta (dev build)
- [x] Executable memory via RW/RX page flipping; x18 preserved via the custom x18 ABI
- [ ] Low 4GB memory and i386 programs: needs the `cross-architecture-support` entitlement
- [x] D3D11 through DXMT on Metal (test program; arm64 and x86_64-through-FEX)
- [ ] First real DX11 game
- [ ] Steam Play integration

### Dev build

Until the entitlement is granted, `scripts/build-wine.sh --dev` builds a variant with
Windows' fixed low addresses moved above 4GB (64-bit programs only). Run it with
`scripts/wine-dev`, which uses a clean environment so host secrets never reach
Windows processes. FEX needs to be registered once per prefix:

```sh
scripts/wine-dev wineboot -i
cp dist-dev/lib/wine/aarch64-windows/lib*fex.dll prefix/dev/drive_c/windows/system32/
scripts/wine-dev reg add 'HKLM\Software\Microsoft\Wow64\amd64' /ve /d libarm64ecfex.dll /f
scripts/wine-dev reg add 'HKLM\Software\Microsoft\Wow64\x86' /ve /d libwow64fex.dll /f
```

First CPU numbers (M2 Pro, same C source, x86_64 through FEX vs native arm64): integer,
memory and contended atomics within noise of native; floating point ~1.4x slower.
