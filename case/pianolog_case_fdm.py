"""
pianolog enclosure, FDM variant: the same board, openings, screws and status light as
pianolog_case.py, reshaped so both shells print on a home filament printer with no support
material at all.

    python pianolog_case_fdm.py        # writes out/fdm_case_top.*, out/fdm_case_bottom.*

What changes against the MJF/SLA case (pianolog_case.py), and why:

* Flat roof and flat bottom instead of the pitched roof and domed bottom. Each shell prints
  with its outside face on the build plate and its open side up, so the roof and the bottom
  are first layers (as smooth as the plate) and nothing inside hangs over air. A 2 mm roof
  slope or a 1 mm dome would show as stair-step contour lines on a filament printer.
* 45-degree chamfers instead of fillets where the roof and the bottom meet the sides. A fillet
  touching the build plate starts as a near-horizontal overhang and curls; a 45-degree chamfer
  prints cleanly and also hides elephant's foot (the squashed first layer).
* The lettering is engraved into the face that lies on the plate. The first layers leave the
  letters out, and the letter floors are short bridges.
* The four screw posts get two fins each, joining them to the nearest walls. A 3.4 mm post
  standing 21 mm tall wobbles while it prints and splits along its layers when a screw cuts
  its thread; the fins brace it. Print with 4 perimeters so the posts come out solid.
* Under each screw head, the step from the 4.2 mm head recess to the 2.4 mm screw hole is
  modelled as two sacrificial bridge layers (the "sequential bridging" trick, for 0.2 mm
  layers), so the step prints as clean bridges instead of a ring of plastic over air.
* The piano (USB-A) opening runs out through the rim as a slot. In the original it leaves a
  0.4 mm strip of wall across the top, which breaks off. (pianolog_case.py has the same fix.)
* The rubber feet stick straight onto the flat bottom, over the screw heads.
"""
import os

import cadquery as cq

import pianolog_case as pc
from pianolog_case import (BOARD, CEIL_Z, CX, CY, ENGRAVE, FLOOR_Z, FONT, HOLES, IN_X0,
                           IN_X1, IN_Y0, IN_Y1, INNER_R, LABEL_ENGRAVE, LABEL_FONT,
                           LABEL_SIZE, LABELS, LED_FLANGE_HOLE, LED_HOLE, LED_LEN, LED_POS,
                           LED_PROUD, LED_SLEEVE_D, OUT, OUTER, PCB_T, PILOT_D, POST_D,
                           SCREW_CLEAR_D, SCREW_HEAD_D, STANDOFF_D, USBA_OPEN, USBA_X, USBA_Z,
                           WALL, WORDMARK, WORDMARK_FIT, CORNER_R)

TOP_Z = CEIL_Z + WALL                   # flat roof
BOT_Z = FLOOR_Z - WALL                  # flat bottom
CHAMFER = 2.0                           # 45-degree, on the two faces that touch the build plate
LAYER = 0.2                             # layer height the sacrificial bridges are drawn for
FIN_T = 1.2                             # three 0.4 mm lines
FIN_GAP = 1.0                           # fins stop this far above the board's top surface
LED_FLANGE_Z = TOP_Z + LED_PROUD - LED_LEN


def outer_body():
    half = OUTER / 2
    body = pc.rounded_box(CX - half, CX + half, CY - half, CY + half, BOT_Z, TOP_Z, CORNER_R)
    return body.faces(">Z or <Z").edges().chamfer(CHAMFER)


def roof_lettering():
    """Engraved volume for the wordmark and port labels, on the flat roof."""
    probe = pc._text(WORDMARK, 10.0, FONT, 0, 0).val().BoundingBox()
    size = 10.0 * min(WORDMARK_FIT[0] / probe.ylen, WORDMARK_FIT[1] / probe.xlen)
    x_mid = CX + OUTER / 2 - CHAMFER - 1.5 - WORDMARK_FIT[1] / 2
    slab = lambda d: cq.Workplane("XY").workplane(offset=TOP_Z - d).rect(200, 200).extrude(d + 1)
    out = pc._text(WORDMARK, size, FONT, x_mid, CY).intersect(slab(ENGRAVE))
    for s, x, y in LABELS:
        out = out.union(pc._text(s, LABEL_SIZE, LABEL_FONT, x, y).intersect(slab(LABEL_ENGRAVE)))
    return out


def led_holder(shell):
    x, y = LED_POS
    wp = lambda z: cq.Workplane("XY").workplane(offset=z).center(x, y)
    sleeve = wp(LED_FLANGE_Z).circle(LED_SLEEVE_D / 2).extrude(CEIL_Z - LED_FLANGE_Z + 0.5)
    bore = wp(LED_FLANGE_Z).circle(LED_HOLE / 2).extrude(30)
    seat = wp(LED_FLANGE_Z - 1).circle(LED_FLANGE_HOLE / 2).extrude(2.05)
    return shell.union(sleeve).cut(bore).cut(seat)


def led_model():
    x, y = LED_POS
    wp = lambda z: cq.Workplane("XY").workplane(offset=z).center(x, y)
    flange = wp(LED_FLANGE_Z).circle(2.9).extrude(1.0)
    body = wp(LED_FLANGE_Z).circle(2.5).extrude(LED_LEN - 2.5)
    dome = cq.Workplane("XY").sphere(2.5).translate((x, y, LED_FLANGE_Z + LED_LEN - 2.5))
    return flange.union(body).union(dome)


def box(x0, x1, y0, y1, z0, z1):
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False)
            .translate((x0, y0, z0)))


def usba_slot():
    """Carry the upright USB-A opening down through the rim, so no thin strip is left."""
    cx, cz = sum(USBA_X) / 2, sum(USBA_Z) / 2
    return box(cx - USBA_OPEN[0] / 2, cx + USBA_OPEN[0] / 2, CY - OUTER, IN_Y0 + 1, -1, cz)


def post_fins(x, y):
    """Two fins from a screw post to the two walls nearest to it."""
    wall_x = IN_X0 - 0.5 if x < BOARD / 2 else IN_X1 + 0.5
    wall_y = IN_Y0 - 0.5 if y < BOARD / 2 else IN_Y1 + 0.5
    z0, z1 = PCB_T + FIN_GAP, CEIL_Z + 0.5
    fx = box(min(x, wall_x), max(x, wall_x), y - FIN_T / 2, y + FIN_T / 2, z0, z1)
    fy = box(x - FIN_T / 2, x + FIN_T / 2, min(y, wall_y), max(y, wall_y), z0, z1)
    return fx.union(fy)


def top_shell(body, holes_cut):
    upper = body.intersect(box(-100, 100, -100, 100, 0, 100))
    shell = upper.cut(pc.cavity()).cut(holes_cut).cut(usba_slot())
    for (x, y) in HOLES:
        post = (cq.Workplane("XY").workplane(offset=PCB_T).center(x, y)
                .circle(POST_D / 2).extrude(CEIL_Z - PCB_T + 0.5))
        shell = shell.union(post).union(post_fins(x, y))
        pilot = cq.Workplane("XY").workplane(offset=PCB_T).center(x, y).circle(PILOT_D / 2).extrude(10)
        shell = shell.cut(pilot)
    return led_holder(shell).cut(roof_lettering())


def screw_hole(x, y):
    """Head recess from the bottom face, then two sacrificial bridge layers, then the
    screw hole up through the standoff. Printed bottom-down, the first bridge layer spans
    the recess leaving a slot as wide as the hole; the second spans the slot at right angles
    leaving a square; the round hole carries on from there."""
    head = (cq.Workplane("XY").workplane(offset=BOT_Z - 1).center(x, y)
            .circle(SCREW_HEAD_D / 2).extrude(FLOOR_Z - BOT_Z + 1))
    slot = box(x - SCREW_HEAD_D / 2 - 0.1, x + SCREW_HEAD_D / 2 + 0.1,
               y - SCREW_CLEAR_D / 2, y + SCREW_CLEAR_D / 2, FLOOR_Z - 0.01, FLOOR_Z + LAYER)
    square = box(x - SCREW_CLEAR_D / 2, x + SCREW_CLEAR_D / 2,
                 y - SCREW_CLEAR_D / 2, y + SCREW_CLEAR_D / 2, FLOOR_Z, FLOOR_Z + 2 * LAYER)
    through = (cq.Workplane("XY").workplane(offset=FLOOR_Z).center(x, y)
               .circle(SCREW_CLEAR_D / 2).extrude(-FLOOR_Z + 1))
    return head.union(slot).union(square).union(through)


def bottom_shell(body, holes_cut):
    lower = body.intersect(box(-100, 100, -100, 100, -100, 0))
    shell = lower.cut(pc.cavity()).cut(holes_cut)
    for (x, y) in HOLES:
        standoff = (cq.Workplane("XY").workplane(offset=FLOOR_Z - 0.5).center(x, y)
                    .circle(STANDOFF_D / 2).extrude(-FLOOR_Z + 0.5))
        shell = shell.union(standoff).cut(screw_hole(x, y))
    return shell


def main():
    os.makedirs(OUT, exist_ok=True)
    body = outer_body()
    holes_cut = pc.openings()
    top = top_shell(body, holes_cut)
    bottom = bottom_shell(body, holes_cut)
    text = roof_lettering().intersect(body)
    board, led = pc.board_model(), led_model()

    for name, shape in (("fdm_case_top", top), ("fdm_case_bottom", bottom),
                        ("fdm_case_top_text", text)):
        cq.exporters.export(shape, os.path.join(OUT, f"{name}.step"))
        pc.export_mesh(shape, os.path.join(OUT, name))
    cq.exporters.export(led, os.path.join(OUT, "fdm_led.stl"), tolerance=0.02, angularTolerance=0.1)

    for name, shape in (("top", top), ("bottom", bottom)):
        v = shape.intersect(board).val().Volume() if shape.intersect(board).vals() else 0.0
        print(f"overlap board/{name}: {v:.3f} mm3")
    v = top.intersect(led).val().Volume() if top.intersect(led).vals() else 0.0
    print(f"overlap led/top: {v:.3f} mm3")
    bb = body.val().BoundingBox()
    print(f"outside: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm")
    print(f"volume: top {top.val().Volume() / 1000:.1f} cm3, bottom {bottom.val().Volume() / 1000:.1f} cm3")
    return top, bottom, board, text, led


if __name__ == "__main__":
    main()
