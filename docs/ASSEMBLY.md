# Assembly

## Printed parts

| File | Size (mm) | Volume | What it is |
|---|---|---|---|
| `tray.stl` | 55.1 × 27.1 × 9.2 | 4.03 cm³ | Board carrier — display window, 3 button holes |
| `backplate.stl` | 55.1 × 31.3 × 8.1 | 9.09 cm³ | Battery pocket, clip pad, 3 charm loops |
| `clip_shoe.stl` | 32.0 × 13.6 × 3.6 | 1.14 cm³ | Saddle the alligator clip bonds into |
| `ALL_moodclip_plate.stl` | 154.2 × 31.3 × 9.2 | — | All three, arranged for one bed |

### Print settings

| Setting | Value |
|---|---|
| Material | PETG or PLA+ |
| Layer height | 0.16 mm |
| Walls | 4 |
| Infill | 30% |
| Supports | **none needed** — all overhangs are flat-bottomed |
| Orientation | As-is. Tray prints window-side down; backplate pocket up. |

The button holes are Ø4.4 mm with 1.2 mm between them. If your printer runs tight
and they close up, ream with a 4.5 mm bit rather than reprinting.

## The USB-C opening

The tray prints **without** a USB-C aperture. Cut it after printing: the port sits
on the short edge, centred, about 9.5 × 3.6 mm. A craft knife and two minutes
does it, or add a negative box in your slicer's modifiers.

This is deliberate. Every way of generating that notch as part of the mesh either
split the wall into a floating shell or broke manifoldness, and shipping a clean
solid you modify beats shipping a subtly broken one. Same call as the butterfly
project's tray.

You may not even want it — the clip charges perfectly well with the board lifted
out of the tray, and leaving the wall solid is stronger.

## Build order

**1. Test before assembly.** Flash the firmware, plug in the battery, confirm all
three buttons work and the mood cycles. Far easier now than after gluing.

**2. Fit the board.** The Feather drops into the tray from the back, display
facing out through the window. It should be a friction fit against the 0.35 mm
clearance. If it's tight, sand the cavity walls — do not force it; the display
glass is right behind that bezel.

Check alignment: the display should sit centred in its window and all three
buttons should be reachable through their holes. Press each one.

**3. Battery into the backplate.** Pouch goes in the pocket, wires routed toward
the board's JST socket. **Do not glue the battery in** — you want to be able to
replace it, and adhesive on a LiPo pouch is a puncture risk.

**4. Connect and close.** Plug the JST into the Feather. Tuck slack wire into the
pocket beside the cell — never pinch it between the two shells. The backplate
sits against the tray; bond with a few small dots of CA at the corners, or leave
it friction-fit if you'd rather keep access.

**5. Clip shoe.** Roughen the flat top of the alligator clip and the channel in
the shoe with sandpaper. Slow epoxy, clamp, full cure. The channel stops the clip
rotating under load, which CA alone won't.

**6. Shoe to backplate.** Bond the shoe to the flat pad on the backplate, aligned
along the long axis so the clip grips parallel to your hair.

**7. Charms.** Jump rings through the three loops along the edge. Keep them light
— a heavy dangle will rotate the whole clip in your hair.

## Wearing it

The reference build wears it **landscape, screen facing outward**, clipped near
the temple with the charms hanging down. Rotation 3 in firmware puts the buttons
along the bottom edge in that orientation, so they're reachable with a thumb.

Clip into a section of hair thick enough to carry ~25 g. A thin wisp will droop.

## Battery safety

- **Verify polarity before the first connection** if you're not using an
  Adafruit-branded cell. No reverse-polarity protection on BAT.
- Charge via USB-C. The yellow CHG LED lights while charging and flickers when no
  battery is attached — the flicker is normal, not a fault.
- Don't charge unattended the first time.
- A puffed or punctured pouch gets retired immediately. Don't fold the pouch to
  make it fit, and don't trap it under a hard edge.
- The firmware warns below 3.45 V, but the protection circuit in the pack is the
  real safety net. Use a protected cell.

## Fit adjustments

All dimensions live in the `P` dict at the top of `cad/make_case.py`:

```python
pcb_l=50.8, pcb_w=22.8,       # board footprint
board_stack=7.0,              # depth of the cavity
scr_l=25.0, scr_w=13.0, scr_x=21.0,   # display window
btn_pitch=5.6, btn_x=6.0, btn_d=4.4,  # button row
bat_l=30.0, bat_w=25.0, bat_t=4.5,    # battery pocket
wall=1.8, fit=0.35,           # wall thickness, clearance
```

Then `python3 make_case.py --out ../stl`.

The generator validates as it builds: area conservation, manifold edges, Euler
characteristic parity, and positive volume. It also **refuses to build** if the
button holes would overlap the display window or fall outside the board cavity —
that check exists because an earlier layout overlapped them by 0.6 mm and would
have printed buttons cutting into the screen bezel.

Anything other than `OK` in the output is a bug, not a warning.
