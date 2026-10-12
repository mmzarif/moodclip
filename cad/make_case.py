#!/usr/bin/env python3
"""
Enclosure for the Mood Hair Clip — Adafruit ESP32-S3 Reverse TFT Feather.

Reuses the validated mesh kernel from the butterfly-clip project (scanline
triangulation + boundary-loop stitching, with area/manifold/volume checks).
See ../../docs/STL.md for why that machinery exists.

    python3 make_case.py --out ../stl

Parts:
  tray        Feather sits face-out; display window, button access, USB-C port
  backplate   Battery pocket + hair-clip mount pad + charm loops; snaps on
  clip_shoe   Small saddle the metal alligator clip glues into

Feather dimensions are from Adafruit's published mechanical data: 50.8 x 22.8 mm
PCB, ~7 mm thick with the display and JST connector.
"""

import argparse
import math
import os
import sys

# Pull in the geometry kernel from the butterfly project.
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..", "..", "cad")))

from make_stl import (  # noqa: E402
    Mesh, slab, slab_poly_pocket, rect, rounded_rect, circle, dedupe,
    as_ccw, as_cw, signed_area, triangulate, boundary_loops, _ear_clip_simple,
    check_watertight, mesh_volume, write_stl,
)

# ─────────────────────────── parameters ───────────────────────────

P = dict(
    # Feather ESP32-S3 Reverse TFT
    pcb_l=50.8, pcb_w=22.8, pcb_t=1.6,
    board_stack=7.0,          # PCB + display + tallest component
    # 1.14" display active area. Pushed toward the USB end of the board so the
    # three buttons get a usable strip beside it. Centring the window left only
    # 11 mm for the button row, which forced holes down to ~3 mm — technically
    # printable, but too small to press reliably with a fingertip.
    scr_l=25.0, scr_w=13.0, scr_x=21.0,
    # Buttons in a row beside the display, toward the antenna end.
    # An earlier layout overlapped them with the window by 0.6 mm; the
    # aperture guard below now rejects that outright.
    btn_pitch=5.6, btn_x=6.0, btn_d=4.4,
    # USB-C on the short edge
    usb_w=9.5, usb_h=3.6,
    # Adafruit LiPo 350-420 mAh pouch (LP402530 class)
    bat_l=30.0, bat_w=25.0, bat_t=4.5,
    # print / fit
    wall=1.8, lip=1.0, fit=0.35,
    # hair clip + charms
    clip_w=10.0, clip_l=32.0,
    charm_loop_d=2.6, charm_loops=3,
)

SEG = 40


def outer_dims():
    """Case footprint derived from the board plus walls and fit clearance."""
    L = P['pcb_l'] + 2 * (P['wall'] + P['fit'])
    W = P['pcb_w'] + 2 * (P['wall'] + P['fit'])
    return L, W


# ─────────────────────────── parts ───────────────────────────

def _assert_apertures_disjoint(win_box, btn_circles, cav_l, cav_w):
    """
    Guard the front-face layout. Overlapping apertures merge into one region,
    which silently breaks the mesh (and would print as buttons cutting into the
    screen bezel). Checked here so a bad parameter edit fails loudly.
    """
    wx0, wy0, wx1, wy1 = win_box
    for i, (cx, cy, r) in enumerate(btn_circles):
        # circle vs axis-aligned rect: nearest point on the rect to the centre
        nx = max(wx0, min(cx, wx1))
        ny = max(wy0, min(cy, wy1))
        if (cx - nx) ** 2 + (cy - ny) ** 2 < r * r:
            raise SystemExit(
                f"button {i} at ({cx:.2f},{cy:.2f}) r={r:.2f} overlaps the "
                f"display window x[{wx0:.2f},{wx1:.2f}] y[{wy0:.2f},{wy1:.2f}]. "
                "Move btn_x / reduce btn_d.")
        if abs(cx) + r > cav_l / 2 or abs(cy) + r > cav_w / 2:
            raise SystemExit(f"button {i} falls outside the board cavity.")
    for i in range(len(btn_circles)):
        for j in range(i + 1, len(btn_circles)):
            ax, ay, ar = btn_circles[i]
            bx, by_, br = btn_circles[j]
            if (ax - bx) ** 2 + (ay - by_) ** 2 < (ar + br) ** 2:
                raise SystemExit(f"buttons {i} and {j} overlap each other.")
    if abs(wx0) > cav_l / 2 or abs(wx1) > cav_l / 2 or \
       abs(wy0) > cav_w / 2 or abs(wy1) > cav_w / 2:
        raise SystemExit("display window falls outside the board cavity.")


def part_tray():
    """
    Board carrier. The Feather drops in from the back; the display faces out
    through a window in the front face.
    """
    L, W = outer_dims()
    wall = P['wall']
    depth = P['board_stack'] + wall + P['fit']

    outer = rounded_rect(-L / 2, -W / 2, L / 2, W / 2, 3.0)
    cav_l = P['pcb_l'] + 2 * P['fit']
    cav_w = P['pcb_w'] + 2 * P['fit']
    cavity = rect(-cav_l / 2, -cav_w / 2, cav_l / 2, cav_w / 2)

    # Front face apertures, cut clean through the wall:
    #   - display window
    #   - three button holes
    sx = P['scr_x'] - P['pcb_l'] / 2          # window, board-relative
    win_cx = sx + P['scr_l'] / 2
    win_box = (win_cx - P['scr_l'] / 2, -P['scr_w'] / 2,
               win_cx + P['scr_l'] / 2, P['scr_w'] / 2)

    bx0 = P['btn_x'] - P['pcb_l'] / 2
    by = 0.0                                   # buttons centred on the long axis
    btn_circles = [(bx0 + i * P['btn_pitch'], by, P['btn_d'] / 2)
                   for i in range(3)]

    _assert_apertures_disjoint(win_box, btn_circles, cav_l, cav_w)

    holes = [as_cw(rounded_rect(*win_box, 1.2))]
    for (cx, cy, r) in btn_circles:
        holes.append(as_cw(circle(cx, cy, r, segs=SEG)))

    m = slab_poly_pocket(outer, 0.0, depth,
                         pocket=cavity,
                         pocket_depth=depth - wall,
                         through_holes=holes)

    # USB-C notch: a separate shell is wrong here (it would float), so the port
    # opening is left to be filed after printing — same tradeoff as the
    # butterfly tray. Documented in STL_MOODCLIP.md.
    return m


def part_backplate():
    """
    Snap-on back: holds the battery, carries the clip mounting pad, and has
    loops along one edge for dangling charms.
    """
    L, W = outer_dims()
    wall = P['wall']
    plate_t = wall + P['bat_t'] + wall

    outer = rounded_rect(-L / 2, -W / 2, L / 2, W / 2, 3.0)
    bat = rect(-P['bat_l'] / 2, -P['bat_w'] / 2 if P['bat_w'] < W - 2 * wall
               else -(W - 2 * wall - 1) / 2,
               P['bat_l'] / 2, P['bat_w'] / 2 if P['bat_w'] < W - 2 * wall
               else (W - 2 * wall - 1) / 2)

    # battery pocket opens toward the tray (up, +Z)
    m = slab_poly_pocket(outer, 0.0, plate_t,
                         pocket=bat,
                         pocket_depth=P['bat_t'])

    # charm loops along the -Y edge, as separate small rings (they are meant to
    # be printed attached; each is its own shell, which slicers handle fine)
    loop_r_out = P['charm_loop_d'] / 2 + 1.1
    for i in range(P['charm_loops']):
        cx = (i - (P['charm_loops'] - 1) / 2.0) * 9.0
        cy = -W / 2 - loop_r_out + 0.6            # slightly overlapping the body
        ring_outer = circle(cx, cy, loop_r_out, segs=SEG)
        ring_hole = circle(cx, cy, P['charm_loop_d'] / 2, segs=SEG, ccw=False)
        m.extend(slab(ring_outer, 0.0, min(plate_t, 2.4),
                      through_holes=[ring_hole]))
    return m


def part_clip_shoe():
    """
    Saddle that the metal alligator clip's flat top glues into, with a shallow
    channel so the clip cannot rotate once bonded.
    """
    wall = P['wall']
    L = P['clip_l']
    W = P['clip_w'] + 2 * wall
    t = 3.6

    outer = rounded_rect(-L / 2, -W / 2, L / 2, W / 2, 2.0)
    channel = rect(-L / 2 + 3.0, -P['clip_w'] / 2, L / 2 - 3.0, P['clip_w'] / 2)
    return slab_poly_pocket(outer, 0.0, t,
                            pocket=channel,
                            pocket_depth=1.6)


PARTS = {
    "tray": part_tray,
    "backplate": part_backplate,
    "clip_shoe": part_clip_shoe,
}


# ─────────────────────────── main ───────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="../stl")
    ap.add_argument("--only")
    args = ap.parse_args()

    outdir = os.path.abspath(args.out)
    os.makedirs(outdir, exist_ok=True)

    L, W = outer_dims()
    print(f"Case footprint: {L:.1f} x {W:.1f} mm")
    print(f"Writing STL to {outdir}\n")

    names = [args.only] if args.only else list(PARTS)
    ok_all = True
    built = {}
    for n in names:
        mesh = PARTS[n]()
        built[n] = mesh
        ok_all = check_watertight(mesh, n) and ok_all
        write_stl(mesh, os.path.join(outdir, f"{n}.stl"), n)

    if not args.only:
        plate = Mesh()
        x = 0.0
        for n in ["tray", "backplate", "clip_shoe"]:
            m = built[n]
            lo, hi = m.bounds()
            plate.extend(m.translate(x - lo[0], -lo[1], -lo[2]))
            x += (hi[0] - lo[0]) + 6.0
        write_stl(plate, os.path.join(outdir, "ALL_moodclip_plate.stl"), "plate")
        lo, hi = plate.bounds()
        print(f"\n  {'plate':<12} {len(plate.tris):6d} tris  "
              f"{hi[0]-lo[0]:6.1f} x {hi[1]-lo[1]:6.1f} x {hi[2]-lo[2]:6.1f} mm")

    print("\n" + ("All parts watertight." if ok_all else "WARNING: open meshes."))


if __name__ == "__main__":
    main()
