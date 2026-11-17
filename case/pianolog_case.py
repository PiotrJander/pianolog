"""
pianolog enclosure for the Waveshare ESP32-P4-NANO (parametric, CadQuery).

    pip install cadquery
    python pianolog_case.py            # writes out/case_top.*, out/case_bottom.*, out/board.step

Two shells split at the underside of the board. Four M2x12 screws go in from below, through
the bottom shell's standoffs and the board's mounting holes, and bite into posts in the top
shell. Self-adhesive rubber feet cover the screw heads.

Coordinates (mm): looking down on the component side of the board, x to the right,
y from the RJ45/USB-A edge (y=0, "front") to the USB-C/microSD edge (y=50, "back"),
z=0 at the underside of the board.

Values marked MEASURE come from photos rather than from Waveshare's dimension drawing.
Measure them on your board with calipers and change them here before printing.
"""
import math
import os

import cadquery as cq

# --------------------------------------------------------------------------------------------
# Board: from Waveshare's dimension drawing (exact)
# --------------------------------------------------------------------------------------------
BOARD = 50.0
BOARD_R = 2.0
PCB_T = 1.6
HOLES = [(2.45, 2.45), (47.55, 2.45), (2.45, 47.54), (47.55, 47.54)]
USBC_X = (20.52, 29.48)                 # drawing: 20.52 from each side

# --------------------------------------------------------------------------------------------
# Board: estimated from photos -- MEASURE
# --------------------------------------------------------------------------------------------
USBC_FACE_Y = 52.1                      # MEASURE: how far the USB-C sticks out past the back edge
USBC_Z = (PCB_T, PCB_T + 3.3)           # MEASURE: USB-C receptacle height above the board
USBA_X = (23.4, 30.3)                   # USB-A stands upright (narrow, tall)
USBA_FACE_Y = -2.7                      # MEASURE: how far the USB-A sticks out past the front edge
USBA_Z = (PCB_T, PCB_T + 15.0)          # MEASURE: USB-A height above the board
RJ45_X = (6.1, 22.3)
RJ45_FACE_Y = -1.6                      # MEASURE
RJ45_H = 13.6                           # MEASURE
ANT_X, ANT_FACE_Y = (31.3, 45.1), -1.9  # Wi-Fi module antenna overhang -- MEASURE
SD_X = (6.3, 18.8)                      # microSD slot under the board, card goes in from y=50
SD_Z = (-1.9, 0.0)                      # MEASURE: slot height under the board
HEADER_H = 8.6                          # pin headers above the board (if fitted)
HEADER_PINS_BELOW = 3.2                 # solder tails below the board -- MEASURE

# --------------------------------------------------------------------------------------------
# Enclosure
# --------------------------------------------------------------------------------------------
CLEAR = 0.7                             # gap around the board edge
FRONT_CLEAR = 0.9                       # gap in front of the furthest-protruding connector
BACK_CLEAR = 0.9
WALL = 2.2
FLOOR_Z = -4.6                          # inner floor (below solder tails)
CEIL_Z = 19.0                           # inner ceiling (above USB-A and its plug opening)
OUTER = 61.0                            # square footprint
CORNER_R = 10.0                         # plan-view corner radius
INNER_R = 4.0
EDGE_FILLET = 3.0                       # rounding where top/bottom meet the sides
DOME_TOP = 1.5                          # top bulges by this much in the middle
DOME_BOTTOM = 1.0

# Openings, sized for the plug bodies (overmolds), not just the receptacles
USBC_OPEN = (13.4, 7.6)                 # width x height, stadium
USBA_OPEN = (9.8, 17.4)                 # upright stadium
SD_OPEN = (15.0, 3.6)

SCREW_CLEAR_D = 2.4                     # M2 through the standoffs
SCREW_HEAD_D = 4.2                      # M2 pan/cheese head
PILOT_D = 1.6                           # M2 thread-forming into plastic
STANDOFF_D = 5.0
POST_D = 3.4                            # thin: the pin headers start 2.45 mm from the holes
FOOT_D = 10.5                           # recess for self-adhesive bumpers of 10 mm
FOOT_RECESS = 0.6

WORDMARK = "maraursa"
FONT = "/usr/share/fonts/opentype/inter/InterDisplay-SemiBold.otf"
FONT_SIZE = 8.0
ENGRAVE = 0.6

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# --------------------------------------------------------------------------------------------
# Derived layout
# --------------------------------------------------------------------------------------------
IN_X0, IN_X1 = -CLEAR, BOARD + CLEAR
IN_Y0 = min(USBA_FACE_Y, RJ45_FACE_Y, ANT_FACE_Y) - FRONT_CLEAR
IN_Y1 = max(USBC_FACE_Y, BOARD) + BACK_CLEAR
CX, CY = (IN_X0 + IN_X1) / 2, (IN_Y0 + IN_Y1) / 2
TOP_EDGE_Z = CEIL_Z + WALL
BOT_EDGE_Z = FLOOR_Z - WALL


def dome_radius(rise, half_width=OUTER / 2):
    return (half_width ** 2 + rise ** 2) / (2 * rise)


R_TOP, R_BOT = dome_radius(DOME_TOP), dome_radius(DOME_BOTTOM)


def bottom_surface_z(x, y):
    """z of the domed underside at (x, y)."""
    r2 = (x - CX) ** 2 + (y - CY) ** 2
    zc = BOT_EDGE_Z - DOME_BOTTOM + R_BOT          # sphere centre
    return zc - math.sqrt(R_BOT ** 2 - r2)


def rounded_box(x0, x1, y0, y1, z0, z1, r):
    return (cq.Workplane("XY").workplane(offset=z0)
            .center((x0 + x1) / 2, (y0 + y1) / 2)
            .rect(x1 - x0, y1 - y0).extrude(z1 - z0)
            .edges("|Z").fillet(r))


def stadium_y(cx, cz, w, h, y0, y1):
    """Stadium-shaped hole through a wall that faces +/-y."""
    r = min(w, h) / 2
    return (cq.Workplane("XZ", origin=(0, y1, 0))
            .center(cx, cz).sketch().rect(w, h).vertices().fillet(r * 0.999).finalize()
            .extrude(y1 - y0))


def outer_body():
    half = OUTER / 2
    body = rounded_box(CX - half, CX + half, CY - half, CY + half,
                       BOT_EDGE_Z - DOME_BOTTOM - 1, TOP_EDGE_Z + DOME_TOP + 1, CORNER_R)
    # Spheres turned on their side, so their poles (where meshers make degenerate triangles)
    # fall far outside the case.
    top = (cq.Workplane("XY").sphere(R_TOP).rotate((0, 0, 0), (1, 0, 0), 90)
           .translate((CX, CY, TOP_EDGE_Z + DOME_TOP - R_TOP)))
    bot = (cq.Workplane("XY").sphere(R_BOT).rotate((0, 0, 0), (1, 0, 0), 90)
           .translate((CX, CY, BOT_EDGE_Z - DOME_BOTTOM + R_BOT)))
    body = body.intersect(top).intersect(bot)
    try:
        body = body.faces(">Z or <Z").edges().fillet(EDGE_FILLET)
    except Exception:  # pragma: no cover - fall back to a smaller rounding if OCC refuses
        body = body.faces(">Z or <Z").edges().fillet(EDGE_FILLET / 2)
    return body


def cavity():
    return rounded_box(IN_X0, IN_X1, IN_Y0, IN_Y1, FLOOR_Z, CEIL_Z, INNER_R)


def openings():
    far_back, far_front = CY + OUTER, CY - OUTER
    usbc = stadium_y(sum(USBC_X) / 2, sum(USBC_Z) / 2, *USBC_OPEN, IN_Y1 - 1, far_back)
    usba = stadium_y(sum(USBA_X) / 2, sum(USBA_Z) / 2, *USBA_OPEN, far_front, IN_Y0 + 1)
    sd = stadium_y(sum(SD_X) / 2, sum(SD_Z) / 2, *SD_OPEN, IN_Y1 - 1, far_back)
    # finger groove under the card slot, so the push-push card can be pressed in and out
    groove = (cq.Workplane("YZ").circle(5.5).extrude(SD_OPEN[0] + 4)
              .translate((sum(SD_X) / 2 - (SD_OPEN[0] + 4) / 2, CY + OUTER / 2 + 4.2, sum(SD_Z) / 2)))
    return usbc.union(usba).union(sd).union(groove)


def wordmark(body):
    text = (cq.Workplane("XY").workplane(offset=TOP_EDGE_Z - 3)
            .center(CX, CY)
            .text(WORDMARK, FONT_SIZE, 10, fontPath=FONT, halign="center", valign="center"))
    skin = body.cut(body.translate((0, 0, -ENGRAVE)))
    return text.intersect(skin)


def top_shell(body, holes_cut):
    upper = body.intersect(cq.Workplane("XY").box(200, 200, 100, centered=(True, True, False)))
    shell = upper.cut(cavity()).cut(holes_cut)
    for (x, y) in HOLES:
        post = (cq.Workplane("XY").workplane(offset=PCB_T).center(x, y)
                .circle(POST_D / 2).extrude(CEIL_Z - PCB_T + 0.5))
        pilot = (cq.Workplane("XY").workplane(offset=PCB_T).center(x, y)
                 .circle(PILOT_D / 2).extrude(10))
        shell = shell.union(post).cut(pilot)
    return shell.cut(wordmark(body))


def bottom_shell(body, holes_cut):
    lower = body.intersect(cq.Workplane("XY").box(200, 200, 100, centered=(True, True, False))
                           .translate((0, 0, -100)))
    shell = lower.cut(cavity()).cut(holes_cut)
    for (x, y) in HOLES:
        standoff = (cq.Workplane("XY").workplane(offset=FLOOR_Z - 0.5).center(x, y)
                    .circle(STANDOFF_D / 2).extrude(-FLOOR_Z + 0.5))
        shell = shell.union(standoff)
        z_pad = bottom_surface_z(x, y) + FOOT_RECESS
        through = cq.Workplane("XY").workplane(offset=-30).center(x, y).circle(SCREW_CLEAR_D / 2).extrude(31)
        head = cq.Workplane("XY").workplane(offset=-30).center(x, y).circle(SCREW_HEAD_D / 2).extrude(FLOOR_Z + 30)
        foot = cq.Workplane("XY").workplane(offset=-30).center(x, y).circle(FOOT_D / 2).extrude(z_pad + 30)
        shell = shell.cut(through).cut(head).cut(foot)
    return shell


def board_model():
    """Rough board for clearance checks and pictures."""
    b = rounded_box(0, BOARD, 0, BOARD, 0, PCB_T, BOARD_R)
    parts = [
        cq.Workplane("XY").box(USBC_X[1] - USBC_X[0], USBC_FACE_Y - 44, USBC_Z[1] - USBC_Z[0], centered=False)
          .translate((USBC_X[0], 44, USBC_Z[0])),
        cq.Workplane("XY").box(USBA_X[1] - USBA_X[0], 20 - USBA_FACE_Y, USBA_Z[1] - USBA_Z[0], centered=False)
          .translate((USBA_X[0], USBA_FACE_Y, USBA_Z[0])),
        cq.Workplane("XY").box(RJ45_X[1] - RJ45_X[0], 21 - RJ45_FACE_Y, RJ45_H, centered=False)
          .translate((RJ45_X[0], RJ45_FACE_Y, PCB_T)),
        cq.Workplane("XY").box(ANT_X[1] - ANT_X[0], 18 - ANT_FACE_Y, 2.6, centered=False)
          .translate((ANT_X[0], ANT_FACE_Y, PCB_T)),
        cq.Workplane("XY").box(SD_X[1] - SD_X[0], 15, SD_Z[1] - SD_Z[0], centered=False)
          .translate((SD_X[0], 35, SD_Z[0])),
    ]
    for x0 in (0.0, 45.0):                         # 2x13 headers, top and solder tails below
        parts.append(cq.Workplane("XY").box(5.0, 33.0, HEADER_H, centered=False).translate((x0, 11.8, PCB_T)))
        parts.append(cq.Workplane("XY").box(5.0, 33.0, HEADER_PINS_BELOW, centered=False)
                     .translate((x0, 11.8, -HEADER_PINS_BELOW)))
    for p in parts:
        b = b.union(p)
    for (x, y) in HOLES:
        b = b.cut(cq.Workplane("XY").center(x, y).circle(1.1).extrude(PCB_T))
    return b


def main():
    os.makedirs(OUT, exist_ok=True)
    body = outer_body()
    holes_cut = openings()
    top = top_shell(body, holes_cut)
    bottom = bottom_shell(body, holes_cut)
    board = board_model()

    for name, shape in (("case_top", top), ("case_bottom", bottom)):
        for ext in ("step", "stl", "3mf"):
            cq.exporters.export(shape, os.path.join(OUT, f"{name}.{ext}"),
                                tolerance=0.02, angularTolerance=0.1) if ext != "step" else \
                cq.exporters.export(shape, os.path.join(OUT, f"{name}.{ext}"))
    cq.exporters.export(board, os.path.join(OUT, "board.step"))

    # Clearance checks against the rough board model
    for name, shape in (("top", top), ("bottom", bottom)):
        v = shape.intersect(board).val().Volume() if shape.intersect(board).vals() else 0.0
        print(f"overlap board/{name}: {v:.3f} mm3")
    bb = body.val().BoundingBox()
    print(f"outside: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm")
    print(f"volume: top {top.val().Volume() / 1000:.1f} cm3, bottom {bottom.val().Volume() / 1000:.1f} cm3")
    return top, bottom, board


if __name__ == "__main__":
    main()
