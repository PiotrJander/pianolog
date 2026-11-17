# pianolog enclosure

![overview](out/overview.png)

A two-part case for the Waveshare ESP32-P4-NANO: 61 × 61 × 30.5 mm, rounded corners,
gently domed top and bottom, `maraursa` engraved on top. Openings for USB-A (piano cable,
front), USB-C (power) and the microSD card (back, with a finger groove).

**Not test-fitted yet.** The board outline, mounting holes and the side-to-side position of
the connectors come from Waveshare's dimension drawing. Connector heights and how far they
stick out past the board edge are estimated from photos (see below). The openings are sized
for plug bodies, which leaves about 1 mm of slack, but measure before paying for a print.

## Files

| File | Use |
|------|-----|
| `out/case_top.step`, `out/case_bottom.step` | for print services (exact curves), and for editing in CAD |
| `out/case_top.3mf`, `out/case_bottom.3mf` | for home slicers (Bambu Studio, PrusaSlicer, Cura); carries units |
| `out/case_top.stl`, `out/case_bottom.stl` | for anything else |
| `out/board.step` | the rough board model used for the clearance check |
| `pianolog_case.py` | the parametric source; `render.py` makes the preview pictures |

```sh
pip install cadquery trimesh
python pianolog_case.py      # rebuilds out/, prints a clearance check against the board model
python render.py             # optional: preview pictures
```

## Measure these first

With calipers, on your board. Change the value in `pianolog_case.py` if it differs by more
than about 0.5 mm, then rebuild.

| Parameter | What to measure | Estimate |
|-----------|-----------------|----------|
| `USBA_FACE_Y` | how far the USB-A socket sticks out past the front edge of the board | 2.7 mm |
| `USBA_Z` | height of the USB-A socket above the board surface | 15.0 mm |
| `USBC_FACE_Y` | how far the USB-C socket sticks out past the back edge | 2.1 mm |
| `USBC_Z` | height of the USB-C socket above the board surface | 3.3 mm |
| `RJ45_H`, `RJ45_FACE_Y` | Ethernet socket height and overhang | 13.6 / 1.6 mm |
| `SD_Z` | height of the microSD slot under the board | 1.9 mm |
| `HEADER_PINS_BELOW` | how far the header pins stick out under the board | 3.2 mm |
| hole diameter | the four corner holes must take an M2 screw | ~2.2 mm |

## Parts

* 4 × M2 × 12 mm screws with a low head (≤ 1.5 mm high, ≤ 4 mm across), e.g. DIN 84 /
  ISO 1207 cheese head. They go in from below, through the bottom shell and the board,
  and cut their own thread in the top shell's posts.
* 4 × self-adhesive rubber feet, 10 mm across, 2.5–3 mm high. They sit over the screw heads
  and keep the domed bottom from rocking.

## Printing

* **Best result: MJF (HP Multi Jet Fusion) in PA12 nylon, dyed black.** Matte, even,
  no layer lines, so the gentle domes come out smooth; tough enough for the thin screw
  posts. Ask for vapor smoothing if you want a satin, near-smooth surface. Send the STEP files.
* **SLA resin, black:** the smoothest and most glossy-capable surface; more brittle, so
  don't overtighten the screws.
* **FDM at home (PETG or PLA, matte black):** cheapest. A gentle dome is the worst case for
  FDM: it shows contour rings. Use 0.1 mm layers (variable layer height) and print both
  parts open side down, with tree supports inside the top shell. Print the bottom shell
  first as a fit test.

## Assembly

1. Put the board into the bottom shell on its four standoffs, microSD slot at the groove.
2. Lower the top shell over it; the USB-A sits in the front opening.
3. Turn over and drive the four M2 × 12 screws until snug. Don't force them: the posts
   in the top shell are 3.4 mm thin, because the pin headers start 2.45 mm from the holes.
4. Stick the rubber feet over the screw heads.
