# Requesting the cross-architecture entitlement

proton-apple needs `com.apple.developer.cross-architecture-support` (or its
`-unmanaged` variant). Without it a native arm64 process cannot map memory below 4GB,
which Windows requires (see [architecture.md](architecture.md)). Everything else can be
developed without it.

Neither key is publicly documented by Apple as of 2026-09-29. They exist in the macOS 27
kernel and in XNU's open-source tests, and are almost certainly what CrossOver's ARM64
preview uses.

## Steps

1. **Enroll** in the Apple Developer Program. An organization enrollment (needs a D-U-N-S
   number) reads as more established than an individual one, but either works.
2. **Register an App ID** for the runtime, e.g. `org.protonapple.runtime` (explicit, not
   wildcard).
3. **Check Capabilities first.** In Certificates, Identifiers & Profiles, open the App ID's
   *Capabilities* tab and look for anything named Cross-Architecture Support. If the
   `-unmanaged` variant is self-serve, it will be here and needs no approval.
4. **Otherwise request it.** Open the App ID's *Capability Requests* tab, find the
   capability and click *Request*. Only the Account Holder can submit. Paste the text below.
5. **If it isn't listed at all**, file a Developer Technical Support request (two are
   included with membership) or a Feedback Assistant report asking how to obtain
   `com.apple.developer.cross-architecture-support`, using the same text. Mention the
   XNU and SDK APIs by name so it reaches the right team.
6. Once assigned, enable it on the App ID, create a **Developer ID** provisioning profile,
   and hand it to `scripts/sign.sh`.

## Request text

> **App:** proton-apple, an open-source compatibility layer that runs Windows games on
> Apple silicon Macs. Source: <REPO URL>. Licence: LGPL (Wine) / MIT (FEX).
>
> **What it is:** a native arm64 build of Wine with FEX, an open-source x86-to-ARM
> translator, loaded through Wine's ARM64EC and WoW64 emulator interfaces. It does not use
> Rosetta, so users keep access to their Windows games after Rosetta's general
> availability ends with macOS 27.
>
> **Why the entitlement is required:**
> - Windows maps `KUSER_SHARED_DATA` at the fixed address `0x7ffe0000`; every Windows
>   program reads it. 32-bit Windows programs (via WoW64) need their entire address space
>   below 4GB, and many executables have fixed low image bases. Native arm64 processes
>   cannot map below 4GB without the soft `__PAGEZERO` this entitlement provides.
> - We also intend to use the related kernel features for x86 translation layers:
>   `posix_spawnattr_set_4k_page_size_np` (Windows assumes 4KB pages),
>   `os_set_custom_x18_abi_enabled` (x18 is the Windows TEB register) and
>   `thread_set_x86_64_compat`.
>
> **Scope and security:** the entitlement is applied only to the Wine loader executable
> inside a signed, notarized app bundle with the hardened runtime enabled. No shells,
> scripting hosts or general-purpose tools receive it. The source is public and builds are
> reproducible.
>
> **Questions:** is the `-unmanaged` variant the right choice for a Developer ID app
> distributed outside the Mac App Store? Does `thread_set_x86_64_compat` enable the TSO
> memory model for the calling thread, and is it intended for third-party translators?
>
> **Contact:** <NAME>, <EMAIL>

## Before submitting

- Publish the repository publicly so the reviewer can see the code.
- Have a working demo recorded (dev mode, see below), even 64-bit only.
- Fill in the placeholders above.

## Developing before approval

- **Dev mode** (no entitlement): Wine's fixed low addresses are relocated above 4GB.
  64-bit programs only. Enough to develop FEX integration, graphics and Steam Play.
- **SIP + AMFI relaxed** on a dev machine: ad-hoc builds may carry the entitlement, so the
  real low-memory path can be tested. See the security trade-offs before doing this on a
  daily-use Mac.
