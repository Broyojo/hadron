# zink-test

Zink (Mesa's OpenGL on Vulkan) on KosmicKrisp, through EGL's surfaceless platform.

    scripts/build-vulkan.sh zink            # Mesa with Zink and EGL into build/mesa-zink-install
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
