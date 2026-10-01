#!/usr/bin/env python3
"""Draw docs/architecture-{light,dark}.svg, the stack diagram in the README.

    tools/arch-diagram/gen.py

Plain SVG, no dependencies. Edit the layout in draw() and run it again."""
import os

THEMES = {
    "light": dict(bg="#ffffff", frame="#d0d7de", text="#1f2328", muted="#59636e", arrow="#59636e",
                  game=("#ffffff", "#1f2328"), platform=("#f0f2f5", "#8c959f"), hadron=("#fff4dc", "#b7791f"),
                  cpu=("#e6f4ea", "#2f8a44"), wine=("#f8e7f0", "#a23b72"), gfx=("#e6effd", "#2f6fdb"),
                  vulkan=("#def4f1", "#14857b"), planned=("none", "#8c959f")),
    "dark": dict(bg="#0d1117", frame="#30363d", text="#e6edf3", muted="#9198a1", arrow="#9198a1",
                 game=("#0d1117", "#e6edf3"), platform=("#21262d", "#6e7681"), hadron=("#33260a", "#d29922"),
                 cpu=("#102a17", "#3fb950"), wine=("#351426", "#db61a2"), gfx=("#0f2644", "#4493f8"),
                 vulkan=("#0a2c29", "#2ec4b6"), planned=("none", "#6e7681")),
}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"
W, H = 1120, 930
X0, X1 = 30, 1090          # content edges
GAP = 14
COLW = (X1 - X0 - 5 * GAP) / 6


def col(i, span=1):
    """x and width of graphics column i, spanning `span` columns"""
    return X0 + i * (COLW + GAP), span * COLW + (span - 1) * GAP


class Svg:
    def __init__(self, theme):
        self.t = THEMES[theme]
        self.out = [
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
            f'font-family="{FONT}" role="img" aria-label="Hadron architecture">',
            f'<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="{self.t["arrow"]}"/></marker></defs>',
            f'<rect x="0.5" y="0.5" width="{W - 1}" height="{H - 1}" rx="12" fill="{self.t["bg"]}" stroke="{self.t["frame"]}"/>',
        ]

    def box(self, x, y, w, h, kind, title=None, sub=None, dashed=False, title_size=15):
        fill, stroke = self.t[kind]
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.out.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" rx="8" fill="{fill}" '
                        f'stroke="{stroke}" stroke-width="1.5"{dash}/>')
        lines = ([(title, title_size, 600, self.t["text"])] if title else []) + \
                [(s, 12, 400, self.t["muted"]) for s in (sub or [])]
        total = sum(size + 5 for _, size, _, _ in lines) - 5
        ty = y + (h - total) / 2
        for text, size, weight, colour in lines:
            ty += size
            self.text(x + w / 2, ty - 2, text, size, weight, colour)
            ty += 5

    def text(self, x, y, text, size=12, weight=400, colour=None, anchor="middle", italic=False):
        style = ' font-style="italic"' if italic else ""
        self.out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" '
                        f'fill="{colour or self.t["muted"]}" text-anchor="{anchor}"{style}>{text}</text>')

    def arrow(self, x1, y1, x2, y2, dashed=False):
        dash = ' stroke-dasharray="5 4"' if dashed else ""
        self.out.append(f'<line x1="{x1:.1f}" y1="{y1}" x2="{x2:.1f}" y2="{y2}" stroke="{self.t["arrow"]}" '
                        f'stroke-width="1.5" marker-end="url(#a)"{dash}/>')

    def path(self, d, dashed=False):
        dash = ' stroke-dasharray="5 4"' if dashed else ""
        self.out.append(f'<path d="{d}" fill="none" stroke="{self.t["arrow"]}" stroke-width="1.5" '
                        f'marker-end="url(#a)"{dash}/>')

    def finish(self):
        return "\n".join(self.out + ["</svg>", ""])


def draw(theme):
    s = Svg(theme)
    cx = [col(i)[0] + COLW / 2 for i in range(6)]   # column centres

    # Steam and the launcher
    s.box(X0, 28, X1 - X0, 56, "platform", "Steam for Mac", ["the native client: library, downloads, friends, cloud saves"])
    launch_w = col(0, 4)[1]
    s.box(X0, 118, launch_w, 56, "hadron", "Hadron Steam Play tool",
          ["steam-run → play: one prefix per game, per-game settings, memory watchdog"])
    s.arrow(X0 + launch_w / 2, 84, X0 + launch_w / 2, 116)
    s.text(X0 + launch_w / 2 + 10, 105, "launches", anchor="start")

    # the game and its code
    gy = 208
    s.box(X0, gy, launch_w, 138, "game")
    s.text(X0 + 16, gy + 24, "Windows game", 15, 600, s.t["text"], anchor="start")
    s.arrow(X0 + launch_w / 2, 174, X0 + launch_w / 2, gy - 2)
    cw = (launch_w - 32 - 2 * GAP) / 3
    code = [("x86-64 code", "FEX (ARM64EC)", "translated to ARM64", "cpu"),
            ("32-bit x86 code", "FEX (WoW64)", "translated to ARM64", "cpu"),
            ("ARM64 code", "runs directly", "no translation", "platform")]
    for i, (name, how, sub, kind) in enumerate(code):
        x = X0 + 16 + i * (cw + GAP)
        s.text(x + cw / 2, gy + 52, name, 13, 600, s.t["text"])
        s.arrow(x + cw / 2, gy + 58, x + cw / 2, gy + 76)
        s.box(x, gy + 78, cw, 46, kind, how, [sub], title_size=14)

    # Wine
    wy = 380
    s.box(X0, wy, X1 - X0, 100, "wine")
    s.text(X0 + 16, wy + 24, "Wine", 15, 600, s.t["text"], anchor="start")
    s.text(X0 + 62, wy + 24, "built natively for arm64 macOS: the Windows API, with no Rosetta underneath", anchor="start")
    s.arrow(X0 + launch_w / 2, gy + 138, X0 + launch_w / 2, wy - 2)
    parts = [("Windows kernel services", "ntdll, kernelbase: memory, threads, files, registry"),
             ("winemac.drv", "windows, input and displays on AppKit"),
             ("lsteamclient", "Steam bridge: forwards Steamworks calls")]
    pw = (X1 - X0 - 32 - 2 * GAP) / 3
    for i, (name, sub) in enumerate(parts):
        s.box(X0 + 16 + i * (pw + GAP), wy + 38, pw, 48, "hadron" if i == 2 else "game", name, [sub], title_size=14)
    # the bridge back up to Steam
    bx = X0 + 16 + 2 * (pw + GAP) + pw / 2
    s.arrow(bx, wy + 38, bx, 86)
    s.text(bx + 10, 240, "Steamworks", anchor="start")
    s.text(bx + 10, 256, "calls", anchor="start")

    # graphics APIs
    ay = 520
    apis = [(0, 1, "Direct3D 9"), (1, 1, "Direct3D 10 / 11"), (2, 1, "Direct3D 12"), (3, 1, "Vulkan"), (4, 2, "OpenGL")]
    for i, span, name in apis:
        x, w = col(i, span)
        s.arrow(x + w / 2, wy + 100, x + w / 2, ay - 2)
        s.text(x + w / 2, ay + 18, name, 14, 600, s.t["text"])

    # translation layers
    ty = 560
    layers = [(0, "gfx", "mtld3d", ["Direct3D 9 on Metal"], False),
              (1, "gfx", "DXMT", ["Direct3D 10/11 on Metal"], False),
              (2, "gfx", "vkd3d-proton", ["Direct3D 12 on Vulkan"], False),
              (3, "gfx", "winevulkan", ["Wine's Vulkan loader"], False),
              (4, "planned", "Zink", ["OpenGL 4.6 on Vulkan", "planned"], True),
              (5, "wine", "Wine OpenGL", ["passes OpenGL through", "used today"], False)]
    for i, kind, name, sub, dashed in layers:
        x, w = col(i)
        s.box(x, ty, w, 64, kind, name, sub, dashed)
    for i in range(4):
        s.arrow(cx[i], ay + 26, cx[i], ty - 2)
    gl_x, gl_w = col(4, 2)
    s.path(f"M{gl_x + gl_w / 2 - 20:.1f},{ay + 26} L{cx[4]:.1f},{ty - 2}", dashed=True)
    s.path(f"M{gl_x + gl_w / 2 + 20:.1f},{ay + 26} L{cx[5]:.1f},{ty - 2}")

    # drivers
    dy = 664
    kx, kw = col(2, 3)
    s.box(kx, dy, kw, 64, "vulkan", "KosmicKrisp", ["Mesa's Vulkan driver on Metal 4, with Hadron's patches"])
    ox, ow = col(5)
    s.box(ox, dy, ow, 64, "platform", "Apple OpenGL", ["version 4.1, deprecated"])
    for i in (2, 3):
        s.arrow(cx[i], ty + 64, cx[i], dy - 2)
    s.arrow(cx[4], ty + 64, cx[4], dy - 2, dashed=True)
    s.arrow(cx[5], ty + 64, cx[5], dy - 2)

    # Metal and the GPU
    my = 768
    s.box(X0, my, X1 - X0, 50, "platform", "Metal", ["Apple's graphics API"])
    for i in (0, 1):
        s.arrow(cx[i], ty + 64, cx[i], my - 2)
    s.arrow(kx + kw / 2, dy + 64, kx + kw / 2, my - 2)
    s.arrow(cx[5], dy + 64, cx[5], my - 2)
    s.text(cx[0] + 10, dy + 36, "direct", anchor="start")
    s.text(cx[1] + 10, dy + 36, "direct", anchor="start")

    # legend
    ly = 850
    s.arrow(W / 2, my + 50, W / 2, ly - 6)
    s.box(X0 + 330, ly - 4, 400, 34, "platform", "Apple silicon GPU", title_size=14)
    legend = [("cpu", "x86 translation"), ("wine", "Wine"), ("gfx", "graphics translation"),
              ("vulkan", "Vulkan driver"), ("hadron", "Hadron's own code"), ("platform", "macOS and Steam")]
    lx = X0
    yy = 904
    for kind, name in legend:
        fill, stroke = s.t[kind]
        s.out.append(f'<rect x="{lx}" y="{yy - 11}" width="14" height="14" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
        s.text(lx + 20, yy, name, anchor="start")
        lx += 20 + len(name) * 6.1 + 30
    s.out.append(f'<rect x="{lx}" y="{yy - 11}" width="14" height="14" rx="3" fill="none" stroke="{s.t["planned"][1]}" '
                 f'stroke-width="1.5" stroke-dasharray="4 3"/>')
    s.text(lx + 20, yy, "planned", anchor="start")
    return s.finish()


if __name__ == "__main__":
    docs = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs")
    for theme in THEMES:
        path = os.path.normpath(os.path.join(docs, f"architecture-{theme}.svg"))
        with open(path, "w") as f:
            f.write(draw(theme))
        print("wrote", path)
