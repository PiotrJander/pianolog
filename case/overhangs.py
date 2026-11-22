"""
Rough FDM printability check: how much surface faces downward more steeply than a
given angle (i.e. would need support), for a part in a given print orientation.

    python overhangs.py out/case_top.stl            # as exported (z up)
    python overhangs.py out/case_bottom.stl --flip  # turned upside down first

Faces lying on the build plate are not counted. Small areas (a few mm2: letter
bridges, tiny chamfers) print fine; large areas mean support material.
"""
import argparse

import numpy as np
import trimesh


def report(path, flip=False, max_angle=45.0, bed_tol=0.05):
    m = trimesh.load(path)
    if flip:
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
    m.apply_translation([0, 0, -m.bounds[0][2]])
    n, area = m.face_normals, m.area_faces
    zc = m.triangles_center[:, 2]
    down = n[:, 2] < -np.cos(np.radians(max_angle))
    on_bed = zc < bed_tol
    need = down & ~on_bed
    flat = need & (n[:, 2] < -0.99)                      # near-horizontal ceilings
    print(f"{path}{' (flipped)' if flip else ''}: "
          f"needs support {area[need].sum():7.1f} mm2, of which flat ceilings {area[flat].sum():7.1f} mm2; "
          f"on the bed {area[on_bed & (n[:, 2] < -0.99)].sum():7.1f} mm2")
    return area[need].sum()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("stl", nargs="+")
    ap.add_argument("--flip", action="store_true")
    a = ap.parse_args()
    for p in a.stl:
        report(p, a.flip)
