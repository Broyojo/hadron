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
```

## Status

- [x] Upstream Wine builds natively for arm64 macOS (aarch64 + arm64ec + i386 PE)
- [x] FEX ARM64EC and WoW64 DLLs build, with a Darwin unix helper
- [ ] Wine starts: needs the restricted `cross-architecture-support` entitlement for low memory
- [~] Executable memory via RW/RX page flipping (written, untested)
- [ ] x86_64 and i386 programs run through FEX
- [ ] DXMT, first DX11 game
- [ ] Steam Play integration
