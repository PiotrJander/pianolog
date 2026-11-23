"""
What both pianolog cases share: the Waveshare ESP32-P4-NANO board, the inside of the case,
the two openings, the screws, the lettering and the status light, plus the export and
clearance-check helpers. The two variants (fdm/case.py and pitched-roof/case.py) differ only
in the outside shape and in how they are printed.

Coordinates (mm): looking down on the component side of the board, x to the right,
y from the RJ45/USB-A edge (y=0, "front") to the USB-C edge (y=50, "back"),
z=0 at the underside of the board. The two shells split at z=0.

Values marked MEASURE come from photos rather than from Waveshare's dimension drawing.
Measure them on your board with calipers, change them here, and rebuild both variants.
"""
import os

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))

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
USBC_FACE_Y = 51.2                      # drawing photo: USB-C overhangs the back edge by about 1.1 mm
USBC_Z = (PCB_T, PCB_T + 3.3)           # MEASURE: USB-C receptacle height above the board
USBA_X = (23.4, 30.3)                   # USB-A stands upright (narrow, tall)
USBA_FACE_Y = -2.7                      # MEASURE: how far the USB-A sticks out past the front edge
USBA_Z = (PCB_T, 20.0)                  # measured: top of the USB-A is 20 mm above the board's underside
RJ45_X = (6.1, 22.3)
RJ45_FACE_Y = -1.6                      # MEASURE
RJ45_H = 13.6                           # MEASURE
ANT_X, ANT_FACE_Y = (30.9, 44.7), -0.5  # drawing: 13.80 wide; the antenna ends about flush with the edge
SD_X = (6.51, 18.7)                     # drawing: slot starts 6.51 from the edge; card goes in from y=50
SD_Z = (-1.9, 0.0)                      # MEASURE: slot height under the board
HEADER_H = 8.6                          # pin headers above the board (if fitted)
HEADER_PINS_BELOW = 3.2                 # 2 mm measured; 3.2 allowed for longer solder tails

# --------------------------------------------------------------------------------------------
# Inside of the case, the same in both variants
# --------------------------------------------------------------------------------------------
CLEAR = 0.7                             # gap around the board edge
FRONT_CLEAR = 0.9                       # gap in front of the furthest-protruding connector
BACK_CLEAR = 0.9
WALL = 2.2
FLOOR_Z = -4.6                          # inner floor, 1.4 mm under the 3.2 mm allowance
CEIL_Z = 22.6                           # inner ceiling: above the USB-A and jumper wires on the headers
OUTER = 61.0                            # square footprint
CORNER_R = 10.0                         # plan-view corner radius
INNER_R = 4.0

# Openings, sized for the plug bodies (overmolds), not just the receptacles. There is no
# microSD opening: the card stays inside (takes download over Wi-Fi); open the case to swap it.
USBC_OPEN = (13.4, 7.6)                 # width x height, stadium
USBA_OPEN = (9.8, 20.8)                 # upright stadium

SCREW_CLEAR_D = 2.4                     # M2 through the standoffs
SCREW_HEAD_D = 4.2                      # M2 pan/cheese head
PILOT_D = 1.6                           # M2 thread-forming into plastic
STANDOFF_D = 5.0
POST_D = 3.4                            # thin: the pin headers start 2.45 mm from the holes

# Lettering on the roof
WORDMARK = "maraursa"
FONT = os.path.join(HERE, "fonts", "Pacifico-Regular.ttf")       # SIL OFL, see fonts/
WORDMARK_FIT = (46.0, 13.0)             # largest wordmark, along y x across
LABEL_FONT = os.path.join(HERE, "fonts", "BebasNeue-Regular.ttf")
LABEL_SIZE = 5.5
LABEL_ENGRAVE = 0.6
LABELS = (("IN", 26.85, 0.6), ("POWER", 25.0, 46.0))  # text, centre x, centre y: next to each port
ENGRAVE = 1.0                           # deep enough to fill with paint, or print in a second colour

# Status light: a 5 mm through-hole WS2812D-F5 pushed up from inside into a sleeve. Its flange
# stops against a step; a drop of glue (or hot glue) on the flange keeps it there.
LED_POS = (11.0, 42.0)                  # back-left corner, over the low LCD/CAM connectors
LED_HOLE = 5.1                          # 5 mm body; ream with a 5 mm drill if the print is tight
LED_FLANGE_HOLE = 6.2                   # 5.8 mm flange
LED_LEN = 8.6                           # flange underside to tip
LED_PROUD = 0.8                         # how far the tip stands above the top surface
LED_SLEEVE_D = 8.6

# --------------------------------------------------------------------------------------------
# Derived layout
# --------------------------------------------------------------------------------------------
IN_X0, IN_X1 = -CLEAR, BOARD + CLEAR
IN_Y0 = min(USBA_FACE_Y, RJ45_FACE_Y, ANT_FACE_Y) - FRONT_CLEAR
IN_Y1 = max(USBC_FACE_Y, BOARD) + BACK_CLEAR
CX, CY = (IN_X0 + IN_X1) / 2, (IN_Y0 + IN_Y1) / 2
TOP_EDGE_Z = CEIL_Z + WALL              # where the sides meet the roof
BOT_EDGE_Z = FLOOR_Z - WALL             # where the sides meet the bottom
X_LEFT, X_RIGHT = CX - OUTER / 2, CX + OUTER / 2

REFERENCE = os.path.join(HERE, "reference")


# --------------------------------------------------------------------------------------------
# Shapes
# --------------------------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False)
            .translate((x0, y0, z0)))


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


def upper_half(body):
    return body.intersect(box(-100, 100, -100, 100, 0, 100))


def lower_half(body):
    return body.intersect(box(-100, 100, -100, 100, -100, 0))


def cavity():
    return rounded_box(IN_X0, IN_X1, IN_Y0, IN_Y1, FLOOR_Z, CEIL_Z, INNER_R)


def openings():
    far_back, far_front = CY + OUTER, CY - OUTER
    usbc = stadium_y(sum(USBC_X) / 2, sum(USBC_Z) / 2, *USBC_OPEN, IN_Y1 - 1, far_back)
    usba = stadium_y(sum(USBA_X) / 2, sum(USBA_Z) / 2, *USBA_OPEN, far_front, IN_Y0 + 1)
    return usbc.union(usba)


def usba_slot():
    """Carry the upright USB-A opening down through the rim as a slot: otherwise a 0.4 mm
    strip of wall is left across it at the split line, which breaks off."""
    cx, cz = sum(USBA_X) / 2, sum(USBA_Z) / 2
    return box(cx - USBA_OPEN[0] / 2, cx + USBA_OPEN[0] / 2, CY - OUTER, IN_Y0 + 1, -1, cz)


def screw_posts(shell, extra=None):
    """Posts hanging from the ceiling, with a pilot hole for the M2 screw to cut its thread.
    extra(x, y) may return more solid to add at each post (the FDM variant's fins)."""
    for (x, y) in HOLES:
        post = (cq.Workplane("XY").workplane(offset=PCB_T).center(x, y)
                .circle(POST_D / 2).extrude(CEIL_Z - PCB_T + 0.5))
        shell = shell.union(post)
        if extra:
            shell = shell.union(extra(x, y))
        pilot = cq.Workplane("XY").workplane(offset=PCB_T).center(x, y).circle(PILOT_D / 2).extrude(10)
        shell = shell.cut(pilot)
    return shell


def standoffs(shell):
    for (x, y) in HOLES:
        shell = shell.union(cq.Workplane("XY").workplane(offset=FLOOR_Z - 0.5).center(x, y)
                            .circle(STANDOFF_D / 2).extrude(-FLOOR_Z + 0.5))
    return shell


def text(s, size, font, x, y, z):
    """Text standing up through the roof (its base 6 mm under z), reading from the front
    edge towards the back. Intersect it with the roof to get the engraved volume."""
    t = (cq.Workplane("XY").text(s, size, 20, fontPath=font, halign="center", valign="center")
         .rotate((0, 0, 0), (0, 0, 1), 90))
    bb = t.val().BoundingBox()
    return t.translate((x - (bb.xmin + bb.xmax) / 2, y - (bb.ymin + bb.ymax) / 2, z - 6))


def wordmark_size():
    probe = text(WORDMARK, 10.0, FONT, 0, 0, 0).val().BoundingBox()
    return 10.0 * min(WORDMARK_FIT[0] / probe.ylen, WORDMARK_FIT[1] / probe.xlen)


def led_holder(shell, flange_z):
    x, y = LED_POS
    wp = lambda z: cq.Workplane("XY").workplane(offset=z).center(x, y)
    sleeve = wp(flange_z).circle(LED_SLEEVE_D / 2).extrude(CEIL_Z - flange_z + 0.5)
    bore = wp(flange_z).circle(LED_HOLE / 2).extrude(30)
    seat = wp(flange_z - 1).circle(LED_FLANGE_HOLE / 2).extrude(2.05)
    return shell.union(sleeve).cut(bore).cut(seat)


def led_model(flange_z):
    """The LED itself, for clearance checks and pictures."""
    x, y = LED_POS
    wp = lambda z: cq.Workplane("XY").workplane(offset=z).center(x, y)
    flange = wp(flange_z).circle(2.9).extrude(1.0)
    body = wp(flange_z).circle(2.5).extrude(LED_LEN - 2.5)
    dome = cq.Workplane("XY").sphere(2.5).translate((x, y, flange_z + LED_LEN - 2.5))
    return flange.union(body).union(dome)


def board_model():
    """Rough board for clearance checks and pictures."""
    b = rounded_box(0, BOARD, 0, BOARD, 0, PCB_T, BOARD_R)
    parts = [
        box(USBC_X[0], USBC_X[1], 44, USBC_FACE_Y, *USBC_Z),
        box(USBA_X[0], USBA_X[1], USBA_FACE_Y, 20, *USBA_Z),
        box(RJ45_X[0], RJ45_X[1], RJ45_FACE_Y, 21, PCB_T, PCB_T + RJ45_H),
        box(ANT_X[0], ANT_X[1], ANT_FACE_Y, 18, PCB_T, PCB_T + 2.6),
        box(SD_X[0], SD_X[1], 35, 50, *SD_Z),
    ]
    for x0 in (0.0, 45.0):                         # 2x13 headers, top and solder tails below
        parts.append(box(x0, x0 + 5, 11.4, 44.4, PCB_T, PCB_T + HEADER_H))
        parts.append(box(x0, x0 + 5, 11.4, 44.4, -HEADER_PINS_BELOW, 0))
    for p in parts:
        b = b.union(p)
    for (x, y) in HOLES:
        b = b.cut(cq.Workplane("XY").center(x, y).circle(1.1).extrude(PCB_T))
    return b


# --------------------------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------------------------
def export_mesh(shape, stem):
    """STL and 3MF. The tessellator can leave zero-area slivers where roundings meet the rim;
    dropping them makes the mesh watertight, which slicers and print services check."""
    import trimesh
    cq.exporters.export(shape, stem + ".stl", tolerance=0.02, angularTolerance=0.1)
    m = trimesh.load(stem + ".stl")
    m.update_faces(m.nondegenerate_faces())
    m.remove_unreferenced_vertices()
    m.merge_vertices()
    assert m.is_watertight, f"{stem}: mesh not watertight"
    m.export(stem + ".stl")
    m.export(stem + ".3mf")


def build(out, body, top, bottom, text_body, led):
    """Write top/bottom/top_text (STEP, STL, 3MF) and led.stl to out/, the board model to
    reference/, and print the clearance check against the board and the LED."""
    os.makedirs(out, exist_ok=True)
    os.makedirs(REFERENCE, exist_ok=True)
    board = board_model()
    for name, shape in (("top", top), ("bottom", bottom), ("top_text", text_body)):
        cq.exporters.export(shape, os.path.join(out, f"{name}.step"))
        export_mesh(shape, os.path.join(out, name))
    cq.exporters.export(led, os.path.join(out, "led.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(board, os.path.join(REFERENCE, "board.step"))

    overlap = lambda a, b: a.intersect(b).val().Volume() if a.intersect(b).vals() else 0.0
    print(f"overlap board/top: {overlap(top, board):.3f} mm3")
    print(f"overlap board/bottom: {overlap(bottom, board):.3f} mm3")
    print(f"overlap led/top: {overlap(top, led):.3f} mm3")
    bb = body.val().BoundingBox()
    print(f"outside: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm")
    print(f"volume: top {top.val().Volume() / 1000:.1f} cm3, bottom {bottom.val().Volume() / 1000:.1f} cm3")
