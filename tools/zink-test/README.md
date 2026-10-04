# zink-test

Zink (Mesa's OpenGL on Vulkan) on KosmicKrisp, through EGL's surfaceless platform.

    scripts/build-vulkan.sh zink            # Mesa with Zink and EGL into dist/mesa-zink
    tools/zink-test/run.sh                  # without a window
    tools/zink-test/run.sh "" window 4      # in a window, for 4 seconds

It creates the newest core context EGL offers, prints the version, lists the extensions each
newer OpenGL version still lacks, creates a compatibility context, and draws a triangle into a
framebuffer object and reads it back.

On 2026-10-02 (M2 Pro, macOS 27.0.1, Mesa patches 0001-0034): OpenGL 3.3 core and 3.3
compatibility, the triangle correct. One extension stands between that and OpenGL 4.6:
`ARB_texture_buffer_object_rgb32`, buffer textures in the three-channel 32-bit formats, which
KosmicKrisp does not offer as texel buffers (Metal has no such pixel format). Zink also warns that
`VK_EXT_custom_border_color` is missing.

`zink-window.m` makes an EGL window surface on a CAMetalLayer (Mesa 0035 and 0036: Zink presents
through a Vulkan swapchain on the layer, as it does for X11, Wayland and Win32 windows). Same day:
a 480x360 window at scale 2 gave a 960x720 surface and 403 frames in 4 s, with the triangle's pixel
right in the back buffer of every frame and no failed swap. What the window showed was not checked
by a person.

`wgl-test.c` is the same triangle as a Windows program, through Wine's WGL (Wine 0023: the Mac
driver's OpenGL on EGL, chosen with `HADRON_OPENGL=zink` in `scripts/play`). Same day, x86-64
under FEX: Apple's OpenGL gives this legacy context "2.1 Metal"; Zink gives "3.3 (Compatibility
Profile)" on "zink Vulkan 1.4(Apple M2 Pro (MESA_KOSMICKRISP))", 396 frames in 4 s, the right
pixel in each. What the window showed was not checked by a person.

`zink-stream.c` changes its vertex data every frame and checks every frame it reads back:

    cc -O1 -o /tmp/zink-stream tools/zink-test/zink-stream.c -Idist/mesa-zink/include \
        -Ldist/mesa-zink/lib -lEGL -Wl,-rpath,$PWD/dist/mesa-zink/lib
    VK_DRIVER_FILES=$PWD/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json \
        MESA_LOADER_DRIVER_OVERRIDE=zink /tmp/zink-stream 400 64 finish streak

Its "streak" mode is what Geometry Dash's ship trail does: between two batches of sprites from an
interleaved buffer, a triangle strip from three separate arrays. That draw came out wrong in every
frame. Zink skipped looking its pipeline up again when only the vertex layout had changed since
the last draw, which goes unnoticed on drivers with `VK_EXT_vertex_input_dynamic_state` (there the
layout is not part of the pipeline) and KosmicKrisp has none. Mesa 0037 looks the pipeline up
again in that case; all modes pass.
