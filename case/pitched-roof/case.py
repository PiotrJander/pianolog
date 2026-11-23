"""
pianolog case, pitched-roof version (the ambitious one): a shallow pitched roof, a gently
domed bottom and soft rounded edges. Meant for powder (MJF, SLS) or resin (SLA) printing,
which need no supports; on a filament printer it would need about 29 cm2 of support per
part and show the roof and the dome as stair steps. For that, use ../fdm.

    python pitched-roof/case.py        # writes pitched-roof/out/top.*, bottom.*, top_text.*, led.stl

Board, inside, openings, screws, lettering and status light come from ../common.py.
"""
import math
import os
import sys

import cadquery as cq

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import common as c  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

EDGE_FILLET = 3.0                       # rounding where roof and bottom meet the sides
RIDGE_X = 33.5                          # the ridge runs front to back here
ROOF_RISE = 2.0                         # ridge height above the top edges
DOME_BOTTOM = 1.0
FOOT_D = 10.5                           # recess for self-adhesive bumpers of 10 mm
FOOT_RECESS = 0.6

R_BOT = (((c.OUTER / 2) ** 2 + DOME_BOTTOM ** 2) / (2 * DOME_BOTTOM))


def bottom_surface_z(x, y):
    """z of the domed underside at (x, y)."""
    r2 = (x - c.CX) ** 2 + (y - c.CY) ** 2
    return c.BOT_EDGE_Z - DOME_BOTTOM + R_BOT - math.sqrt(R_BOT ** 2 - r2)


def top_surface_z(x, y):
    """z of the roof at (x, y), away from the edge rounding."""
    run = (RIDGE_X - c.X_LEFT) if x <= RIDGE_X else (c.X_RIGHT - RIDGE_X)
    return c.TOP_EDGE_Z + ROOF_RISE * (1 - abs(x - RIDGE_X) / run)


LED_FLANGE_Z = top_surface_z(*c.LED_POS) + c.LED_PROUD - c.LED_LEN


def outer_body():
    half = c.OUTER / 2
    body = c.rounded_box(c.CX - half, c.CX + half, c.CY - half, c.CY + half,
                         c.BOT_EDGE_Z - DOME_BOTTOM - 1, c.TOP_EDGE_Z + ROOF_RISE + 1, c.CORNER_R)
    # Pitched roof: a prism along y whose two faces slope down from the ridge to the top edges.
    m = 20.0
    sl = ROOF_RISE / (RIDGE_X - c.X_LEFT)
    sr = ROOF_RISE / (c.X_RIGHT - RIDGE_X)
    pts = [(c.X_LEFT - m, c.BOT_EDGE_Z - 20), (c.X_RIGHT + m, c.BOT_EDGE_Z - 20),
           (c.X_RIGHT + m, c.TOP_EDGE_Z - sr * m), (RIDGE_X, c.TOP_EDGE_Z + ROOF_RISE),
           (c.X_LEFT - m, c.TOP_EDGE_Z - sl * m)]
    top = cq.Workplane("XZ", origin=(0, c.CY + c.OUTER, 0)).polyline(pts).close().extrude(2 * c.OUTER)
    # Domed bottom: a sphere turned on its side, so its poles (where meshers make degenerate
    # triangles) fall far outside the case.
    bot = (cq.Workplane("XY").sphere(R_BOT).rotate((0, 0, 0), (1, 0, 0), 90)
           .translate((c.CX, c.CY, c.BOT_EDGE_Z - DOME_BOTTOM + R_BOT)))
    body = body.intersect(top).intersect(bot)
    for r in (EDGE_FILLET, EDGE_FILLET * 0.75, EDGE_FILLET / 2):
        try:
            return body.edges("not |Z").fillet(r)      # top and bottom rims, and a soft ridge
        except Exception:  # pragma: no cover - OCC sometimes refuses a radius
            continue
    raise RuntimeError("could not round the case edges")


def lettering(body):
    """Wordmark on the narrow roof side plus port labels, as the engraved volume."""
    x_mid = (RIDGE_X + c.X_RIGHT - EDGE_FILLET) / 2 + 0.5
    word = c.text(c.WORDMARK, c.wordmark_size(), c.FONT, x_mid, c.CY, c.TOP_EDGE_Z)
    deep = body.cut(body.translate((0, 0, -c.ENGRAVE)))
    shallow = body.cut(body.translate((0, 0, -c.LABEL_ENGRAVE)))
    out = word.intersect(deep)
    for s, x, y in c.LABELS:
        out = out.union(c.text(s, c.LABEL_SIZE, c.LABEL_FONT, x, y, c.TOP_EDGE_Z).intersect(shallow))
    return out


def top_shell(body, holes_cut):
    shell = c.upper_half(body).cut(c.cavity()).cut(holes_cut).cut(c.usba_slot())
    shell = c.screw_posts(shell)
    return c.led_holder(shell, LED_FLANGE_Z).cut(lettering(body))


def bottom_shell(body, holes_cut):
    shell = c.standoffs(c.lower_half(body).cut(c.cavity()).cut(holes_cut))
    for (x, y) in c.HOLES:
        z_pad = bottom_surface_z(x, y) + FOOT_RECESS
        wp = cq.Workplane("XY").workplane(offset=-30).center(x, y)
        through = wp.circle(c.SCREW_CLEAR_D / 2).extrude(31)
        head = cq.Workplane("XY").workplane(offset=-30).center(x, y).circle(c.SCREW_HEAD_D / 2).extrude(c.FLOOR_Z + 30)
        foot = cq.Workplane("XY").workplane(offset=-30).center(x, y).circle(FOOT_D / 2).extrude(z_pad + 30)
        shell = shell.cut(through).cut(head).cut(foot)
    return shell


def main():
    body = outer_body()
    holes_cut = c.openings()
    top = top_shell(body, holes_cut)
    bottom = bottom_shell(body, holes_cut)
    c.build(OUT, body, top, bottom, lettering(body), c.led_model(LED_FLANGE_Z))


if __name__ == "__main__":
    main()
