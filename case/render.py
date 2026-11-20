"""Shaded preview images of the enclosure: a tiny z-buffer renderer (numpy only, no GPU)."""
import os
import numpy as np, trimesh
from PIL import Image, ImageDraw, ImageFont
import cadquery as cq

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
S = 2                                    # supersampling

def load(name, offset=(0, 0, 0)):
    path = os.path.join(OUT, name)
    if name.endswith(".step"):
        stl = os.path.join(OUT, "_preview_" + os.path.basename(path)[:-5] + ".stl")
        cq.exporters.export(cq.importers.importStep(path), stl, tolerance=0.05, angularTolerance=0.2)
        path = stl
    m = trimesh.load(path, force="mesh"); m.apply_translation(offset); return m

def view_matrix(elev, azim):
    e, a = np.radians(elev), np.radians(azim)
    d = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])   # towards the camera
    up = np.array([0, 0, 1.0]) if abs(e) < np.radians(89) else np.array([0, 1.0, 0])
    r = np.cross(up, d); r /= np.linalg.norm(r); u = np.cross(d, r)
    return np.stack([r, u, d]), d

def render(parts, elev, azim, size=700, light=(-0.35, -0.55, 0.85)):
    R, d = view_matrix(elev, azim)
    allv = np.vstack([p[0].vertices for p in parts])
    c = (allv.max(0) + allv.min(0)) / 2
    pv = (allv - c) @ R.T; span = np.abs(pv[:, :2]).max() * 1.12
    W = size * S; scale = W / (2 * span)
    img = np.full((W, W, 3), (233, 236, 238), float); zb = np.full((W, W), -np.inf)
    L = np.array(light) / np.linalg.norm(light)
    for part in parts:
        m, base = part[0], part[1]
        glow = len(part) > 2 and part[2]
        P = (m.vertices - c) @ R.T
        X = P[:, 0] * scale + W / 2; Y = W / 2 - P[:, 1] * scale; Z = P[:, 2]
        n = m.face_normals
        vis = n @ d > 0                                          # back-face culling
        diff = np.clip(n @ L, 0, 1); spec = np.clip(n @ ((L + d) / np.linalg.norm(L + d)), 0, 1) ** 24
        shade = (0.30 + 0.70 * diff)[:, None] * np.array(base) + (0.22 * spec)[:, None]
        if glow: shade = np.array(base)[None, :] * (0.85 + 0.15 * diff)[:, None] + (0.35 * spec)[:, None]
        for f in np.nonzero(vis)[0]:
            i0, i1, i2 = m.faces[f]
            x = np.array([X[i0], X[i1], X[i2]]); y = np.array([Y[i0], Y[i1], Y[i2]]); z = np.array([Z[i0], Z[i1], Z[i2]])
            xmin, xmax = max(int(x.min()), 0), min(int(x.max()) + 1, W - 1)
            ymin, ymax = max(int(y.min()), 0), min(int(y.max()) + 1, W - 1)
            if xmin > xmax or ymin > ymax: continue
            den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(xmin, xmax + 1) + 0.5, np.arange(ymin, ymax + 1) + 0.5)
            w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
            w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
            w2 = 1 - w0 - w1
            inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not inside.any(): continue
            zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
            sub = zb[ymin:ymax + 1, xmin:xmax + 1]
            upd = inside & (zz > sub)
            sub[upd] = zz[upd]
            img[ymin:ymax + 1, xmin:xmax + 1][upd] = np.clip(shade[f], 0, 1) * 255
    im = Image.fromarray(img.astype(np.uint8)).resize((size, size), Image.LANCZOS)
    return im

def label(im, text):
    d = ImageDraw.Draw(im)
    try: f = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-Medium.otf", 20)
    except Exception: f = ImageFont.load_default()
    d.text((18, 14), text, fill=(40, 44, 48), font=f); return im

# Colours: a soft sky-blue case, black lettering, a green status light.
BLUE = (0.56, 0.78, 0.93); INK = (0.08, 0.08, 0.10); LED = (0.45, 1.0, 0.45); PCB = (0.10, 0.11, 0.12)
top, bottom = load("case_top.stl"), load("case_bottom.stl")
text, led = load("case_top_text.stl", (0, 0, 0.04)), load("led.stl")
lid = [(top, BLUE), (text, INK), (led, LED, True)]
views = [
    ("view_front.png", lid + [(bottom, BLUE)], 24, -115, "front: USB-A for the piano cable"),
    ("view_back.png", lid + [(bottom, BLUE)], 18, 65, "back: USB-C power, microSD with finger groove"),
    ("view_top.png", lid, 72, -90, "top: pitched roof, wordmark and port labels, status light"),
    ("view_exploded.png", [(bottom, BLUE), (load("board.step", (0, 0, 14)), PCB), (load("led.stl", (0, 0, 40)), LED, True),
                           (load("case_top.stl", (0, 0, 40)), BLUE), (load("case_top_text.stl", (0, 0, 40.04)), INK)],
     16, -120, "exploded: bottom, board, LED, top"),
    ("view_bottom.png", lid + [(bottom, BLUE)], -40, -115, "underside: screws under the rubber feet"),
]
ims = []
for fname, parts, el, az, title in views:
    im = label(render(parts, el, az), title); im.save(os.path.join(OUT, fname)); ims.append(im)
w = ims[0].width
sheet = Image.new("RGB", (w * 2, w * 3), (233, 236, 238))
for k, im in enumerate(ims): sheet.paste(im, ((k % 2) * w, (k // 2) * w))
sheet.save(os.path.join(OUT, "overview.png"))
for f in os.listdir(OUT):
    if f.startswith("_preview_"): os.remove(os.path.join(OUT, f))
print("ok")
