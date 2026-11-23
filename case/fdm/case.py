"""
pianolog case, FDM version (the main one): prints on a home filament printer with no support
material at all.

    python fdm/case.py        # writes fdm/out/top.*, bottom.*, top_text.*, led.stl

Board, inside, openings, screws, lettering and status light come from ../common.py. What this
version does with them, and why:

* Flat roof and flat bottom. Each shell prints with its outside face on the build plate and
  its open side up, so the roof and the bottom are first layers (as smooth as the plate) and
  nothing inside hangs over air.
* 45-degree chamfers, not fillets, where the roof and the bottom meet the sides. A fillet
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
* The piano (USB-A) opening runs out through the rim as a slot.
* The rubber feet stick straight onto the flat bottom, over the screw heads.
"""
import os
import sys

import cadquery as cq

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import common as c  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

TOP_Z = c.TOP_EDGE_Z                    # flat roof
BOT_Z = c.BOT_EDGE_Z                    # flat bottom
CHAMFER = 2.0                           # 45-degree, on the two faces that touch the build plate
LAYER = 0.2                             # layer height the sacrificial bridges are drawn for
FIN_T = 1.2                             # three 0.4 mm lines
FIN_GAP = 1.0                           # fins stop this far above the board's top surface
LED_FLANGE_Z = TOP_Z + c.LED_PROUD - c.LED_LEN


def outer_body():
    half = c.OUTER / 2
    body = c.rounded_box(c.CX - half, c.CX + half, c.CY - half, c.CY + half, BOT_Z, TOP_Z, c.CORNER_R)
    return body.faces(">Z or <Z").edges().chamfer(CHAMFER)


def lettering():
    """Engraved volume for the wordmark and port labels, on the flat roof."""
    x_mid = c.CX + c.OUTER / 2 - CHAMFER - 1.5 - c.WORDMARK_FIT[1] / 2
    slab = lambda d: cq.Workplane("XY").workplane(offset=TOP_Z - d).rect(200, 200).extrude(d + 1)
    out = c.text(c.WORDMARK, c.wordmark_size(), c.FONT, x_mid, c.CY, TOP_Z).intersect(slab(c.ENGRAVE))
    for s, x, y in c.LABELS:
        out = out.union(c.text(s, c.LABEL_SIZE, c.LABEL_FONT, x, y, TOP_Z).intersect(slab(c.LABEL_ENGRAVE)))
    return out


def post_fins(x, y):
    """Two fins from a screw post to the two walls nearest to it."""
    wall_x = c.IN_X0 - 0.5 if x < c.BOARD / 2 else c.IN_X1 + 0.5
    wall_y = c.IN_Y0 - 0.5 if y < c.BOARD / 2 else c.IN_Y1 + 0.5
    z0, z1 = c.PCB_T + FIN_GAP, c.CEIL_Z + 0.5
    fx = c.box(min(x, wall_x), max(x, wall_x), y - FIN_T / 2, y + FIN_T / 2, z0, z1)
    fy = c.box(x - FIN_T / 2, x + FIN_T / 2, min(y, wall_y), max(y, wall_y), z0, z1)
    return fx.union(fy)


def top_shell(body, holes_cut):
    shell = c.upper_half(body).cut(c.cavity()).cut(holes_cut).cut(c.usba_slot())
    shell = c.screw_posts(shell, extra=post_fins)
    return c.led_holder(shell, LED_FLANGE_Z).cut(lettering())


def screw_hole(x, y):
    """Head recess from the bottom face, then two sacrificial bridge layers, then the
    screw hole up through the standoff. Printed bottom-down, the first bridge layer spans
    the recess leaving a slot as wide as the hole; the second spans the slot at right angles
    leaving a square; the round hole carries on from there."""
    head = (cq.Workplane("XY").workplane(offset=BOT_Z - 1).center(x, y)
            .circle(c.SCREW_HEAD_D / 2).extrude(c.FLOOR_Z - BOT_Z + 1))
    slot = c.box(x - c.SCREW_HEAD_D / 2 - 0.1, x + c.SCREW_HEAD_D / 2 + 0.1,
                 y - c.SCREW_CLEAR_D / 2, y + c.SCREW_CLEAR_D / 2, c.FLOOR_Z - 0.01, c.FLOOR_Z + LAYER)
    square = c.box(x - c.SCREW_CLEAR_D / 2, x + c.SCREW_CLEAR_D / 2,
                   y - c.SCREW_CLEAR_D / 2, y + c.SCREW_CLEAR_D / 2, c.FLOOR_Z, c.FLOOR_Z + 2 * LAYER)
    through = (cq.Workplane("XY").workplane(offset=c.FLOOR_Z).center(x, y)
               .circle(c.SCREW_CLEAR_D / 2).extrude(-c.FLOOR_Z + 1))
    return head.union(slot).union(square).union(through)


def bottom_shell(body, holes_cut):
    shell = c.standoffs(c.lower_half(body).cut(c.cavity()).cut(holes_cut))
    for (x, y) in c.HOLES:
        shell = shell.cut(screw_hole(x, y))
    return shell


def main():
    body = outer_body()
    holes_cut = c.openings()
    top = top_shell(body, holes_cut)
    bottom = bottom_shell(body, holes_cut)
    c.build(OUT, body, top, bottom, lettering().intersect(body), c.led_model(LED_FLANGE_Z))


if __name__ == "__main__":
    main()
