"""Turn the skin artwork into what the plugin uses.

    python tools/prepare_skin.py [Resources/skin_source.png]

- Finds every control on the artwork (starting from the layout's approximate positions and refining
  them on the image), so small changes between generated versions are picked up automatically.
- Erases the painted knob pointers (the plugin draws live ones) and writes Resources/skin.png.
- Writes Source/SkinLayout.h with all positions, in artwork pixels (1024 x 1536).

A skin with a different layout needs its approximate positions in GUESS updated first.
"""
import pathlib
import sys

import matplotlib.image as mpimg
import numpy as np
from scipy import ndimage
from scipy import signal as sps

ROOT = pathlib.Path(__file__).resolve().parent.parent
T = np.linspace(0, 2 * np.pi, 180, endpoint=False)

# Approximate layout of the "cozy" skin: name -> (x, y, min radius, max radius)
GUESS = {
    "knobs": {
        "input": (207, 105, 40, 58), "gate": (359, 105, 40, 58), "blend": (664, 106, 40, 58),
        "output": (816, 106, 40, 58), "chill": (509, 510, 105, 130), "order": (260, 752, 100, 125),
        "sloth": (760, 752, 100, 125), "rise": (511, 778, 50, 62),
    },
    "buttons": {"prev": (312, 269, 36, 48), "next": (711, 270, 36, 48)},
    "led": (512, 1165, 12, 30),
    "switches": {"up": (180, 1296), "wayUp": (815, 1299)},
    "meters": {"left": 86, "right": 937},
    "presetWindow": (360, 230, 666, 302),  # x0, y0, x1, y1 of the picture inside the frame
    "toggle": (466, 326, 562, 368),        # chrome housing of the mono/stereo toggle
    "toggleLever": (527, 346, 14, 21),     # its ball lever, drawn in the Stereo position
}


# Live pointer proportions (fractions of the knob radius are applied in the plugin) for knobs
# drawn without a pointer: in/out radius and width as fractions of the top face radius.
DEFAULT_POINTER_FRACTIONS = {"in": 0.30, "out": 0.86, "width": 0.075}
DEFAULT_POINTER = {}


def edge_map(grey):
    smooth = ndimage.gaussian_filter(grey, 1.2)
    return np.hypot(ndimage.sobel(smooth, 0), ndimage.sobel(smooth, 1))


def fit_circle(img, cx, cy, rmin, rmax, search=12, arc=None, maximize=True):
    angles = T if arc is None else np.linspace(arc[0], arc[1], 90)
    best = None
    for x in range(cx - search, cx + search + 1):
        for y in range(cy - search, cy + search + 1):
            for r in np.arange(rmin, rmax, 0.5):
                v = ndimage.map_coordinates(img, [y + r * np.sin(angles), x + r * np.cos(angles)], order=1).mean()
                s = v if maximize else -v
                if best is None or s > best[0]:
                    best = (s, x, y, r)
    return best[1:]


def knob_and_pointer(grey, edges, guess):
    x, y, rmin, rmax = guess
    # The upper arc of the silhouette is the top face; the lower part also shows the skirt.
    cx, cy, r = fit_circle(edges, x, y, rmin, rmax, arc=(np.pi * 1.05, np.pi * 1.95), search=8)
    DEFAULT_POINTER.update({k: v * r for k, v in DEFAULT_POINTER_FRACTIONS.items()})
    band = grey[int(cy - r - 8):int(cy + 6), int(cx - 14):int(cx + 15)]
    dark = band < 0.3
    ys, xs = np.nonzero(dark)
    if len(ys) < 20:
        # No painted pointer: the plugin draws one with standard proportions.
        return (float(cx), float(cy), float(r)), dict(DEFAULT_POINTER), np.zeros(grey.shape, bool)
    mask = np.zeros(grey.shape, bool)
    mask[int(cy - r - 8):int(cy + 6), int(cx - 14):int(cx + 15)] = dark
    pointer = {
        "out": float(min(cy - (cy - r - 8 + ys.min()), r * 0.96)),
        "in": float(max(cy - (cy - r - 8 + ys.max()), 0.0)),
        "width": float(np.median([dark[row].sum() for row in np.unique(ys)])),
    }
    return (float(cx), float(cy), float(r)), pointer, ndimage.binary_dilation(mask, iterations=4)


def erase(image, mask, cx, cy):
    """Fill masked pixels with the same knob seen at nearby angles (same radius), then diffuse the rest."""
    yy, xx = np.nonzero(mask)
    ang = np.arctan2(yy - cy, xx - cx)
    rad = np.hypot(yy - cy, xx - cx)
    acc = np.zeros((len(yy), 3))
    cnt = np.zeros(len(yy))
    h, w = mask.shape
    for degrees in [25, 32, 40, 50, 62, 75, 90]:
        for d in np.radians([-degrees, degrees]):
            sx, sy = cx + rad * np.cos(ang + d), cy + rad * np.sin(ang + d)
            ok = ~mask[np.clip(np.round(sy).astype(int), 0, h - 1), np.clip(np.round(sx).astype(int), 0, w - 1)] & (cnt < 4)
            if ok.any():
                for c in range(3):
                    acc[ok, c] += ndimage.map_coordinates(image[:, :, c], [sy[ok], sx[ok]], order=1)
                cnt[ok] += 1
    good = cnt > 0
    image[yy[good], xx[good]] = acc[good] / cnt[good][:, None]
    if (~good).any():
        y0, y1, x0, x1 = yy.min() - 3, yy.max() + 4, xx.min() - 3, xx.max() + 4
        sub = image[y0:y1, x0:x1].copy()
        rest = np.zeros(mask.shape, bool)
        rest[yy[~good], xx[~good]] = True
        m = rest[y0:y1, x0:x1]
        for _ in range(400):
            avg = (np.roll(sub, 1, 0) + np.roll(sub, -1, 0) + np.roll(sub, 1, 1) + np.roll(sub, -1, 1)) / 4
            sub[m] = avg[m]
        image[y0:y1, x0:x1] = sub


def switch_opening(grey, edges, x, y):
    """Footswitch: the cap's rim is the clearest circle. The moving part (cap, bevel highlight, wall
    and the thin dark line around them) is a circle centred a little up and right of the rim's
    centre, 1.165x its radius - fitted to that dark line on the left switch and used for both,
    since the switches are drawn the same way."""
    cx, cy, r = fit_circle(edges, x, y, 38, 50, search=12)
    return [float(cx + 2.9), float(cy - 1.9), float(r * 1.165)]


def meter_lamps(rgb, x):
    """Lamps are the bright runs between the housing's dark gaps along the column's centre."""
    column = ndimage.gaussian_filter1d(rgb[30:340, x - 3:x + 4].max(axis=2).mean(axis=1), 1.0)
    bright = column > 0.5
    ys, y = [], 0
    while y < len(bright):
        if bright[y]:
            start = y
            while y < len(bright) and bright[y]:
                y += 1
            centre = 30 + (start + y - 1) // 2
            # A lamp has the dark housing on both sides; the cream panel above and below the
            # column touches the scan's ends instead.
            if 8 <= y - start <= 30 and start > 0 and y < len(bright):
                ys.append(centre)
        y += 1
    # Column centre from the dark housing in the gap between the two middle lamps; lamps are
    # 20 px wide around it.
    mid = len(ys) // 2
    gap_y = (ys[mid - 1] + ys[mid]) // 2
    dark = np.nonzero(rgb[gap_y, x - 30:x + 30].max(axis=1) < 0.35)[0] + x - 30
    centre = 0.5 * (dark.min() + dark.max())
    return ys, (int(round(centre - 10)), int(round(centre + 10)))


def preset_window(grey):
    """The preset display: a dark rectangle between the arrow buttons (falls back to the guess)."""
    x0, y0, x1, y1 = GUESS["presetWindow"]
    region = grey[y0 - 20:y1 + 20, x0 - 20:x1 + 20] < 0.25
    if region.mean() < 0.3:
        return GUESS["presetWindow"]
    rows = np.nonzero(region.mean(axis=1) > 0.5)[0]
    cols = np.nonzero(region.mean(axis=0) > 0.5)[0]
    return (int(x0 - 20 + cols.min()), int(y0 - 20 + rows.min()), int(x0 - 20 + cols.max()), int(y0 - 20 + rows.max()))


def toggle_axis(grey, lever, rect):
    """The vertical line the toggle's housing is symmetric about (lever excluded), so mirroring the
    housing for Mono moves only the lever."""
    x0, y0, x1, y1 = rect[0] - 11, rect[1] - 4, rect[2] + 13, rect[3] + 4
    edges = edge_map(grey)
    yy, xx = np.mgrid[y0:y1, x0:x1]
    best = None
    for axis in np.arange(0.5 * (rect[0] + rect[2]) - 8, 0.5 * (rect[0] + rect[2]) + 8, 0.25):
        mx = 2 * axis - xx
        ok = (mx >= x0) & (mx < x1) & (np.hypot(xx - lever[0], yy - lever[1]) > lever[2] + 3) \
             & (np.hypot(mx - lever[0], yy - lever[1]) > lever[2] + 3)
        diff = np.abs(edges[yy, xx] - ndimage.map_coordinates(edges, [yy, mx], order=1))[ok].mean()
        if best is None or diff < best[0]:
            best = (diff, float(axis))
    return best[1]


def switch_axis(grey, cx, cy, r):
    """Footswitch: the vertical axis of the dark line around the cap, from rows across its top."""
    smooth = ndimage.gaussian_filter(grey, 0.6)
    axes = []
    for f in (0.62, 0.7, 0.78):
        y = int(round(cy - r * f))
        row = smooth[y]

        def edge(direction):
            xs = np.arange(int(cx), int(cx) + direction * 75, direction)
            prof = row[xs]
            start = int(r * 0.3)
            seg = prof[start:]
            ib = int(np.argmax(seg[:30]))
            return xs[start + ib + int(np.argmin(seg[ib:ib + 20]))]

        axes.append(0.5 * (edge(-1) + edge(1)))
    return float(np.median(axes))


def main():
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "Resources/skin_source.png"
    rgb = mpimg.imread(source)[:, :, :3].astype(np.float64)
    assert rgb.shape[:2] == (1536, 1024), "the skin must be 1024 x 1536"
    grey = rgb.mean(axis=2)
    edges = edge_map(grey)

    knobs, clean = {}, rgb.copy()
    for name, guess in GUESS["knobs"].items():
        circle, pointer, mask = knob_and_pointer(grey, edges, guess)
        if mask.any():
            erase(clean, mask, circle[0], circle[1])
        knobs[name] = (circle, pointer)
        print(f"knob   {name:7s} centre ({circle[0]:.0f}, {circle[1]:.0f}) r {circle[2]:.0f}  pointer {pointer}")

    buttons = {}
    for name, (x, y, a, b) in GUESS["buttons"].items():
        buttons[name] = [float(v) for v in fit_circle(edges, x, y, a, b, search=6)]
        print(f"button {name:7s} {buttons[name]}")
    x, y, a, b = GUESS["led"]
    led = [float(v) for v in fit_circle(edges, x, y, a, b)]
    print(f"led    {led}")
    switches = {name: switch_opening(grey, edges, x, y) for name, (x, y) in GUESS["switches"].items()}
    print(f"switch {switches}")
    meters = {name: meter_lamps(rgb, x) for name, x in GUESS["meters"].items()}
    window = preset_window(grey)
    lx, ly, la, lb = GUESS["toggleLever"]
    lever = [float(v) for v in fit_circle(edges, lx, ly, la, lb, search=4)]
    print(f"lever  {lever}")
    mirror_x = toggle_axis(grey, lever, GUESS["toggle"])
    print(f"toggle mirror axis {mirror_x}")
    axes = {name: switch_axis(grey, *switches[name]) for name in switches}
    print(f"switch axes {axes}")
    print(f"window {window}")
    print(f"meters {meters}")

    mpimg.imsave(ROOT / "Resources/skin.png", np.clip(clean, 0, 1))

    def knob_line(name):
        (cx, cy, r), p = knobs[name]
        return f"inline constexpr Knob {name} {{ {cx:.1f}f, {cy:.1f}f, {r:.1f}f, {p['in']:.1f}f, {p['out']:.1f}f, {p['width']:.1f}f }};"

    def circle_line(kind, name, c):
        return f"inline constexpr {kind} {name} {{ {c[0]:.1f}f, {c[1]:.1f}f, {c[2]:.1f}f }};"

    lamps = meters["left"][0]
    header = [
        "// Generated by tools/prepare_skin.py from Resources/skin_source.png. Do not edit by hand.",
        "// Positions in artwork pixels (1024 x 1536).",
        "#pragma once",
        "",
        "namespace SkinLayout",
        "{",
        "inline constexpr float width = 1024.0f, height = 1536.0f;",
        "",
        "// Knob: rotation centre and radius of the top face, painted pointer from `in` to `out` (radius), width.",
        "struct Knob { float cx, cy, radius, pointerIn, pointerOut, pointerWidth; };",
        *[knob_line(n) for n in GUESS["knobs"]],
        "",
        "struct Circle { float cx, cy, radius; };",
        circle_line("Circle", "prevButton", buttons["prev"]),
        circle_line("Circle", "nextButton", buttons["next"]),
        circle_line("Circle", "led", led),
        "// Footswitch: opening around cap and bevel; the whole content slides when pressed.",
        circle_line("Circle", "upSwitch", switches["up"]),
        circle_line("Circle", "wayUpSwitch", switches["wayUp"]),
        "// Each switch's vertical axis; the mask's left half is mirrored about it to make the right half.",
        f"inline constexpr float upSwitchAxis = {axes['up']:.1f}f, wayUpSwitchAxis = {axes['wayUp']:.1f}f;",
        "",
        "struct Rect { float x0, y0, x1, y1; };",
        "inline constexpr Rect presetWindow {{ {}f, {}f, {}f, {}f }};".format(*[float(v) for v in window]),
        f"inline constexpr bool presetWindowIsDark = {'true' if window != GUESS['presetWindow'] else 'false'};",
        "inline constexpr Rect toggle {{ {}f, {}f, {}f, {}f }};".format(*[float(v) for v in GUESS["toggle"]]),
        "// The toggle's ball lever as drawn (Stereo); Mono moves it to the mirror position in the slot.",
        "// Mono mirrors the housing about this vertical line; the housing maps onto itself, the lever moves.",
        f"inline constexpr float toggleMirrorX = {mirror_x:.2f}f;",
        "",
        "// Meter lamps, top to bottom (same rows on both sides), and each column's x span.",
        f"inline constexpr int meterLamps = {len(lamps)};",
        "inline constexpr float meterLampY[meterLamps] { " + ", ".join(f"{y:.0f}.0f" for y in lamps) + " };",
        "inline constexpr Rect meterLeft {{ {}.0f, {}.0f, {}.0f, {}.0f }};".format(meters["left"][1][0], lamps[0], meters["left"][1][1], lamps[-1]),
        "inline constexpr Rect meterRight {{ {}.0f, {}.0f, {}.0f, {}.0f }};".format(meters["right"][1][0], lamps[0], meters["right"][1][1], lamps[-1]),
        "} // namespace SkinLayout",
        "",
    ]
    (ROOT / "Source/SkinLayout.h").write_text("\n".join(header))
    print("wrote Resources/skin.png and Source/SkinLayout.h")


if __name__ == "__main__":
    main()
