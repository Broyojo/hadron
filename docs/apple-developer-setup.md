# Apple developer setup

Hadron's Wine loader needs `com.apple.developer.cross-architecture-support` so it can
map memory below 4GB, which Windows requires (see [architecture.md](architecture.md)).
In the developer portal this is the **Cross-architecture Compatibility Framework**
capability. As of 2026-09-29 it is self-serve: enable it on the App ID, no request or
approval needed. It still has to come from a provisioning profile, because AMFI kills
ad-hoc signed binaries that carry it.

## Identifiers

| App ID | Purpose | Cross-architecture capability |
|---|---|---|
| `com.broyojo.hadron` | Main app (UI, Steam Play setup) | No |
| `com.broyojo.hadron.loader` | Wine loader bundle that runs games | **Yes** |

Restricted entitlements can only be carried by a bundle with an embedded provisioning
profile, so the loader ships as a small `.app` inside the main app, the same way
CrossOver wraps its loader in `wine.app`.

## One-time setup

1. **App IDs:** Certificates, Identifiers & Profiles -> Identifiers -> + -> App IDs -> App.
   Register both IDs above as explicit bundle IDs.
2. **Capability:** on `com.broyojo.hadron.loader`, enable *Cross-architecture
   Compatibility Framework* and save.
3. **Certificates:** Xcode -> Settings -> Accounts -> Manage Certificates -> + :
   *Apple Development* (local testing) and *Developer ID Application* (distribution).
4. **Devices:** register each development Mac (Devices -> + -> macOS) using its
   Provisioning UDID from `system_profiler SPHardwareDataType`.
5. **Profiles:**
   - *macOS App Development* profile for the loader App ID, with the development
     certificate and registered Macs. Used for local testing, no SIP changes needed.
   - *Developer ID* profile for the loader App ID, for builds distributed to others.

## Verifying a profile

```sh
security cms -D -i Hadron_Loader_Dev.provisionprofile | plutil -p - | grep -A12 Entitlements
```

The entitlements must include `com.apple.developer.cross-architecture-support`
(or the `-unmanaged` variant).

## Before any public release

- Notarize every build (`xcrun notarytool`) with the hardened runtime enabled.
- The entitlement goes only on the loader bundle, never on shells, scripting hosts or
  general-purpose tools.
