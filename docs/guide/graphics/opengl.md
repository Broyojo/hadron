# OpenGL

**Runs on [Zink](https://docs.mesa3d.org/drivers/zink.html), Mesa's OpenGL on Vulkan, on
KosmicKrisp, on Metal.**

Apple's own OpenGL stops at version 4.1, with no compatibility profile above 2.1, so Hadron does
not use it. OpenGL games run on Zink instead, which implements OpenGL on top of Vulkan, here on
KosmicKrisp. That gives OpenGL 4.6, core and compatibility profiles.

## Games on this path

Geometry Dash, 5D Chess With Multiverse Time Travel and Half-Life run; so does Unvanquished, which
is not on Steam. The
[list of games tried](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) has the
details.

Slay the Spire also draws with OpenGL, but it does not start yet, for a reason that has nothing
to do with OpenGL as far as is known: it shows a Java exception at start-up, and Java's compiled
code is known to misbehave under the x86 translator.

## How it is tested

Zink on KosmicKrisp is checked against the Khronos OpenGL and OpenGL ES conformance suites.
[The results](https://github.com/Broyojo/hadron/blob/main/docs/conformance.md) list the gaps that
are left.

## Good to know

- **Shaders Metal refuses** are saved in `~/Library/Logs/Hadron/failed-msl`, and
  [a report](../troubleshooting#save-a-report) includes the newest ones.
- MetalFX upscaling and `HADRON_VSYNC` do not apply here.
