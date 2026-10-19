"""
Generates the Sesame "Aurora head" parts: a round head shell for the Waveshare
ESP32-S3-Touch-AMOLED-1.43C (the cased puck, D50.2 x 14.5 mm) and a top cover
with a neck socket for it.

Run:   python generate_aurora_head.py [--out DIR] [--base No-Ears.stl]
Needs: trimesh, manifold3d, shapely, numpy   (pip install trimesh manifold3d shapely)

Body coordinate system (same as the STLs in hardware/printing/stl):
  X = body length, -X is the front (where the ears/OLED used to be)
  Y = up
  Z = body width, the body centre line is at Z = 2.4
"""
import argparse
import os

import numpy as np
import trimesh
from shapely.geometry import Polygon, box
from trimesh.creation import extrude_polygon, revolve

# --------------------------------------------------------------------------- parameters
PUCK_D, PUCK_T = 50.2, 14.5          # Waveshare 1.43C cased board
PUCK_CLR = 0.3                       # radial / axial clearance around it
HEAD_R, HEAD_T = 30.0, 20.0          # shell outer radius and axial thickness
FRONT_FILLET, BACK_FILLET = 6.0, 1.5
BEZEL_WALL = 2.5                     # front wall that retains the puck
BEZEL_OPEN_D = 45.0                  # opening in front of the glass
CAP_T = 2.7                          # back cap thickness (sits flush in a counterbore)
CAP_D = 56.0
SCREW_HOLE_D = 1.7                   # M2 self tapping
SCREW_R = 27.7
CAP_SCREW_ANGLES = (90, 210, 330)    # deg, 90 = top. Avoids the stem (270) and side windows (0/180)
EAR_ANGLES = (38, -38)               # deg from top
EAR_W, EAR_H, EAR_T, EAR_TIP_R = 22.0, 17.0, 12.0, 3.5
SIDE_WIN_W, SIDE_WIN_H = 14.0, 9.0   # button windows at 3 and 9 o'clock (tangential x axial)
VENT_W, VENT_H, VENT_ANGLES = 7.0, 5.0, (245, 295)  # mic / speaker vents beside the stem
STEM_W, STEM_T = 21.0, 17.5          # neck stem: width (Z) x thickness (X), flush with the head's back face
STEM_ZC = -HEAD_T / 2 + STEM_T / 2   # stem centre on the head axis (local z)
CHAN_W, CHAN_FRONT = 15.0, 4.6       # cable slot through the stem: width, and how far forward of the axis it reaches
TONGUE_W, TONGUE_T = 18.0, 2.7       # back-cap tongue that closes the slot
PLUG_W, PLUG_T, PLUG_L = 12.5, 7.0, 22.0  # dummy USB-C overmould for the clearance check
NECK_EXPOSED, SOCKET_DEPTH = 4.0, 16.0
STEM_CLR = 0.25                      # per side, stem to socket
HEAD_TILT_DEG = 5.0                  # face tilted up, pivoting at the collar top
BODY_ZC = 2.4
HEAD_X = 8.0                         # head centre X (the roof starts at X = -3)
COVER_STRETCH = 15.0                 # extra body height: the cover is cut at COVER_CUT_Y and a straight band inserted
COVER_CUT_Y = 34.0                   # above the mounting lip and the hip notches, below the old OLED window
ROOF_Y = 55.6 + COVER_STRETCH
COLLAR_TOP = ROOF_Y + 4.0
COLLAR_BOTTOM = ROOF_Y - 11.6        # extends into the cover cavity for a deep socket
STEM_SCREW_Y = ROOF_Y + 2.3
STEM_LEN = NECK_EXPOSED + SOCKET_DEPTH


# --------------------------------------------------------------------------- helpers
def U(*ms):
    return trimesh.boolean.union(list(ms), engine='manifold')


def D(a, *ms):
    return trimesh.boolean.difference([a, *ms], engine='manifold')


def I(a, b):
    return trimesh.boolean.intersection([a, b], engine='manifold')


def rrect_prism(w, h, r, z0, z1):
    """Rounded rectangle in XY (outer size w x h, corner radius r) extruded from z0 to z1."""
    poly = box(-w / 2 + r, -h / 2 + r, w / 2 - r, h / 2 - r).buffer(r, join_style=1, resolution=24)
    m = extrude_polygon(poly, z1 - z0)
    m.apply_translation([0, 0, z0])
    return m


def rrect_prism_y(w, d, r, y0, y1):
    """Rounded rectangle in XZ (w along X, d along Z) extruded along Y from y0 to y1."""
    m = rrect_prism(w, d, r, y0, y1)
    # extrude axis z -> y: (x, y, z) -> (x, z, -y) is rot(+90 about x) mirrored; use rot(-90) then mirror z
    m.apply_transform(rot([1, 0, 0], -90))   # (x,y,z) -> (x, z, -y): z range -> y range, y -> -z
    m.vertices[:, 2] *= -1                   # mirror so the XZ polygon keeps its orientation
    m.fix_normals()
    return m


def cyl(r, z0, z1, sections=160):
    m = trimesh.creation.cylinder(radius=r, height=z1 - z0, sections=sections)
    m.apply_translation([0, 0, (z0 + z1) / 2])
    return m


def box_mesh(sx, sy, sz, center=(0, 0, 0)):
    m = trimesh.creation.box(extents=[sx, sy, sz])
    m.apply_translation(center)
    return m


def rot(axis, deg, point=(0, 0, 0)):
    return trimesh.transformations.rotation_matrix(np.radians(deg), axis, point)


def rounded_disc(R, T, r_front, r_back, n=40):
    """Revolved solid: cylinder radius R from z=-T/2 to T/2 with filleted edges (front = +z)."""
    pts = [(0.0, -T / 2)]
    for a in np.linspace(-90, 0, n):
        pts.append((R - r_back + r_back * np.cos(np.radians(a)), -T / 2 + r_back + r_back * np.sin(np.radians(a))))
    for a in np.linspace(0, 90, n):
        pts.append((R - r_front + r_front * np.cos(np.radians(a)), T / 2 - r_front + r_front * np.sin(np.radians(a))))
    pts.append((0.0, T / 2))
    return revolve(np.array(pts), sections=192)


def ear(angle_deg):
    """Rounded triangle growing radially out of the shell (head-local coords, y up)."""
    base_y = HEAD_R - 6.0                      # overlaps the shell so the union is solid
    tri = Polygon([(-EAR_W / 2, base_y - 4), (EAR_W / 2, base_y - 4), (0, base_y + EAR_H)])
    tri = tri.buffer(-EAR_TIP_R, join_style=1).buffer(EAR_TIP_R, join_style=1, resolution=24)
    m = extrude_polygon(tri, EAR_T)
    m.apply_translation([0, 0, -EAR_T / 2])
    m.apply_transform(rot([0, 0, 1], -angle_deg))
    return m


def at_angle(m, r, a_deg):
    m.apply_translation([r * np.cos(np.radians(a_deg)), r * np.sin(np.radians(a_deg)), 0])
    return m


# --------------------------------------------------------------------------- head (local frame)
# local: z = face axis (+z is the front), y = up, x = sideways. Face at z = +HEAD_T/2.
def stem_local(extra=0.0, extend_down=0.0):
    """Neck stem hanging from the shell bottom, y from -(R+STEM_LEN) up to -(R-5) (overlaps the shell)."""
    s = rrect_prism(STEM_W + 2 * extra, STEM_T + 2 * extra, 3.0 + extra,
                    -(HEAD_R + STEM_LEN + extend_down), -(HEAD_R - 5.0))
    s.apply_translation([0, -STEM_ZC, 0])    # polygon y maps to -z below
    s.apply_transform(rot([1, 0, 0], -90))   # (x, y, z) -> (x, z, -y): extrusion -> y, thickness -> z
    return s


def build_head_local():
    zf = HEAD_T / 2
    shell = U(rounded_disc(HEAD_R, HEAD_T, FRONT_FILLET, BACK_FILLET), ear(EAR_ANGLES[0]), ear(EAR_ANGLES[1]), stem_local())

    pocket_r = PUCK_D / 2 + PUCK_CLR
    pocket_front = zf - BEZEL_WALL
    cuts = [cyl(pocket_r, -zf - 1, pocket_front),                 # puck pocket, open at the back
            cyl(BEZEL_OPEN_D / 2, pocket_front - 1, zf + 1)]      # bezel opening
    # chamfer on the bezel opening (D45 inside -> D48 at the face)
    cone = trimesh.creation.cone(radius=BEZEL_OPEN_D / 2 + 1.5 + 20, height=20 + 1.5, sections=160)
    cone.apply_translation([0, 0, zf - 1.5])
    cuts.append(I(cone, cyl(BEZEL_OPEN_D / 2 + 1.5, zf - 1.5, zf + 1)))
    cuts.append(cyl(CAP_D / 2 + 0.2, -zf - 1, -zf + CAP_T))     # counterbore for the back cap
    # cable slot: from inside the pocket straight down through the stem, open at the back so the
    # puck can be inserted with its USB-C plug already fitted. The back-cap tongue closes it.
    y_top, y_bot = -(pocket_r - 3), -(HEAD_R + STEM_LEN + 2)
    z_back = -zf - 1
    cuts.append(box_mesh(CHAN_W, y_top - y_bot, CHAN_FRONT - z_back, center=(0, (y_top + y_bot) / 2, (CHAN_FRONT + z_back) / 2)))
    y_rt = -(HEAD_R - 6)
    cuts.append(box_mesh(TONGUE_W + 0.5, y_rt - y_bot, (-zf + TONGUE_T) - z_back,
                         center=(0, (y_rt + y_bot) / 2, ((-zf + TONGUE_T) + z_back) / 2)))   # tongue recess
    # side windows for the two buttons (3 o'clock), mirrored at 9 o'clock so the puck can face either way
    for sgn in (1, -1):
        cuts.append(box_mesh(14, SIDE_WIN_W, SIDE_WIN_H, center=(sgn * HEAD_R, 0, 0)))
    # vents beside the stem for the mic hole / speaker port on the puck's bottom edge
    for a in VENT_ANGLES:
        v = box_mesh(VENT_W, 14, VENT_H, center=(0, HEAD_R, 0))
        v.apply_transform(rot([0, 0, 1], a - 90))
        cuts.append(v)
    for a in CAP_SCREW_ANGLES:                                     # blind screw holes from the back
        cuts.append(at_angle(cyl(SCREW_HOLE_D / 2, -zf - 1, -zf + CAP_T + 6.0, sections=32), SCREW_R, a))
    body = D(shell, *cuts)

    # back cap: flat disc plus a tongue running down the back of the stem, 3 screws with a shallow head recess
    cap = cyl(CAP_D / 2, -zf, -zf + CAP_T - 0.1)
    y_t0, y_t1 = -(HEAD_R + STEM_LEN - 1.0), -(HEAD_R - 8)
    cap = U(cap, box_mesh(TONGUE_W, y_t1 - y_t0, CAP_T - 0.1, center=(0, (y_t0 + y_t1) / 2, -zf + (CAP_T - 0.1) / 2)))
    for a in CAP_SCREW_ANGLES:
        cap = D(cap, at_angle(cyl(1.15, -zf - 1, -zf + CAP_T + 1, sections=32), SCREW_R, a),
                at_angle(cyl(2.1, -zf - 1, -zf + 1.0, sections=32), SCREW_R, a))
    return body, cap


def head_world_transform():
    """local (x,y,z) -> world: x->Z, y->Y, z->-X, then tilt about the collar top, then translate."""
    M = np.eye(4)
    M[:3, :3] = np.array([[0, 0, -1], [0, 1, 0], [1, 0, 0]])   # columns = images of ex, ey, ez
    center_y = COLLAR_TOP + NECK_EXPOSED + HEAD_R
    T = trimesh.transformations.translation_matrix([HEAD_X, center_y, BODY_ZC])
    tilt = rot([0, 0, 1], -HEAD_TILT_DEG, point=[HEAD_X, COLLAR_TOP, BODY_ZC])
    return tilt @ T @ M


def stem_screw_hole(r):
    sh = cyl(r, HEAD_X - STEM_ZC + 1.0, HEAD_X + 30, sections=32)   # back half only: collar wall, tongue, stem wall
    sh.apply_transform(rot([0, 1, 0], 90))   # axis z -> x
    sh.apply_translation([0, STEM_SCREW_Y, BODY_ZC])
    return sh


# --------------------------------------------------------------------------- cover
def stretch_cover(cov, y_cut, amount):
    """Split the shell at y_cut, lift the upper half by `amount` and fill the gap with a straight band."""
    if amount <= 0:
        return cov
    ov = 0.05   # tiny overlap so the band's faces are not coincident with the cut caps (keeps the STL manifold)
    lower = trimesh.intersections.slice_mesh_plane(cov, [0, -1, 0], [0, y_cut - ov, 0], cap=True)
    upper = trimesh.intersections.slice_mesh_plane(cov, [0, 1, 0], [0, y_cut + ov, 0], cap=True)
    upper.apply_translation([0, amount, 0])
    sec = cov.section(plane_origin=[0, y_cut, 0], plane_normal=[0, 1, 0])
    planar, to3d = sec.to_2D()
    band = U(*[extrude_polygon(poly, amount + 2 * ov + 0.2) for poly in planar.polygons_full])
    band.apply_transform(to3d)
    band.apply_translation([0, (y_cut - ov - 0.1) - band.bounds[0][1], 0])   # to3d may point the extrusion down
    return U(lower, band, upper)


def build_cover(base_path):
    cov = stretch_cover(trimesh.load(base_path), COVER_CUT_Y, COVER_STRETCH)
    S = COVER_STRETCH
    # fill the old OLED window: sample the front face around it and fit a plane
    ys = np.arange(30 + S, 54 + S, 0.5)
    zs = np.arange(-14, 19, 0.5)
    Y, Z = np.meshgrid(ys, zs, indexing='ij')
    o = np.c_[np.full(Y.size, -40.0), Y.ravel(), Z.ravel()]
    loc, idx, _ = cov.ray.intersects_location(o, np.tile([1.0, 0, 0], (len(o), 1)), multiple_hits=False)
    hx = np.full(Y.size, np.nan)
    hx[idx] = loc[:, 0]
    hx = hx.reshape(Y.shape)
    face = (hx < -3.5) & (np.abs(Z - 2.6) < 13) & (Y > 32 + S) & (Y < 50 + S)
    P = np.c_[hx[face], Y[face], Z[face]]
    c = P.mean(0)
    n = np.linalg.svd(P - c)[2][2]
    if n[0] > 0:
        n = -n                                                   # outward normal points to -X
    win = (np.isnan(hx) | (hx > -3.5)) & (Y > 34 + S) & (Y < 51 + S) & (Z > -11) & (Z < 16)
    wy0, wy1 = Y[win].min() - 1.5, Y[win].max() + 1.5
    wz0, wz1 = Z[win].min() - 1.5, Z[win].max() + 1.5
    up = np.array([0, 1.0, 0])
    up -= (up @ n) * n
    up /= np.linalg.norm(up)
    side = np.cross(n, up)
    cy, cz = (wy0 + wy1) / 2, (wz0 + wz1) / 2
    cx = c[0] - (n[1] * (cy - c[1]) + n[2] * (cz - c[2])) / n[0]
    slab = box_mesh(wz1 - wz0, wy1 - wy0, 1.6)                 # x: side, y: up, z: normal
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2] = side, up, n
    M[:3, 3] = np.array([cx, cy, cz]) - n * 0.85
    slab.apply_transform(M)
    cov = U(cov, slab)

    # stepped collar on the roof, reaching into the cavity so the socket is deep
    cx = HEAD_X - STEM_ZC                                       # local z -> world -X
    collar_hi = rrect_prism_y(STEM_T + 4, STEM_W + 4, 4.0, COLLAR_TOP - 2.0, COLLAR_TOP)
    collar_mid = rrect_prism_y(STEM_T + 6, STEM_W + 6, 5.0, ROOF_Y - 3.0, COLLAR_TOP - 2.0)
    # below the roof the collar tapers to a 2 mm wall around the socket so it is nearly self supporting
    lo_top = rrect_prism_y(STEM_T + 6, STEM_W + 6, 5.0, ROOF_Y - 3.0, ROOF_Y - 2.0)
    lo_bot = rrect_prism_y(STEM_T + 2 * STEM_CLR + 4, STEM_W + 2 * STEM_CLR + 4, 3.0, COLLAR_BOTTOM, COLLAR_BOTTOM + 1)
    collar_lo = trimesh.convex.convex_hull(np.vstack([lo_top.vertices, lo_bot.vertices]))
    for cpart in (collar_lo, collar_mid, collar_hi):
        cpart.apply_translation([cx, 0, BODY_ZC])
    cov = U(cov, collar_lo, collar_mid, collar_hi)
    socket = stem_local(STEM_CLR, extend_down=10)
    socket.apply_transform(head_world_transform())
    cov = D(cov, socket, stem_screw_hole(SCREW_HOLE_D / 2 + 0.2))
    return cov, (wy0, wy1, wz0, wz1)


def puck_world():
    zc = HEAD_T / 2 - BEZEL_WALL - PUCK_CLR / 2 - PUCK_T / 2
    p = cyl(PUCK_D / 2, -PUCK_T / 2, PUCK_T / 2)
    p.apply_translation([0, 0, zc])
    plug = box_mesh(PLUG_W, PLUG_L, PLUG_T, center=(0, -PUCK_D / 2 - PLUG_L / 2 + 1.0, zc))  # 1 mm inserted into the port
    p = U(p, plug)
    p.apply_transform(head_world_transform())
    return p


def build_all(base):
    body, cap = build_head_local()
    W = head_world_transform()
    body_w = body.copy()
    body_w.apply_transform(W)
    body_w = D(body_w, stem_screw_hole(SCREW_HOLE_D / 2))
    cap_w = cap.copy()
    cap_w.apply_transform(W)
    cap_w = D(cap_w, stem_screw_hole(SCREW_HOLE_D / 2))
    cover, win = build_cover(base)
    return cover, body_w, cap_w, puck_world(), W, win


def print_oriented(m, W):
    """Head parts flat on the bed: back face down, +Z up."""
    p = m.copy()
    p.apply_transform(np.linalg.inv(W))
    p.apply_translation([0, 0, -p.bounds[0][2]])
    return p


def vol(m):
    return m.volume if len(m.faces) else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=None)
    ap.add_argument('--base', default=None)
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
    covers = os.path.join(repo, 'hardware', 'printing', 'stl', 'top-covers')
    base = a.base or os.path.join(covers, 'Top-Cover-No-Ears-v100.stl')
    out = a.out or os.path.join(covers, 'aurora-head')
    os.makedirs(out, exist_ok=True)

    cover, body_w, cap_w, puck, W, win = build_all(base)
    print_oriented(body_w, W).export(os.path.join(out, 'Aurora-Head-Body-v100.stl'))
    print_oriented(cap_w, W).export(os.path.join(out, 'Aurora-Head-Back-Cap-v100.stl'))
    cover.export(os.path.join(out, 'Top-Cover-Aurora-v100.stl'))
    trimesh.Scene({'cover': cover, 'head': body_w, 'cap': cap_w, 'puck': puck}).export(
        os.path.join(out, 'Aurora-Head-Assembly-v100.glb'))

    for f in ('Aurora-Head-Body-v100.stl', 'Aurora-Head-Back-Cap-v100.stl', 'Top-Cover-Aurora-v100.stl'):
        r = trimesh.load(os.path.join(out, f))
        print(f'{f}: reloaded watertight={r.is_watertight}')
    print('filled window Y %.1f..%.1f  Z %.1f..%.1f' % win)
    for name, m in [('head body', body_w), ('back cap', cap_w), ('cover', cover)]:
        print(f'{name}: watertight={m.is_watertight} volume={m.volume:.0f} mm3 bounds={np.round(m.bounds, 1).tolist()}')
        print(f'   puck+plug overlap {vol(I(m, puck)):.2f} mm3')
    print(f'head/cover overlap {vol(I(body_w, cover)):.2f} mm3, head/cap overlap {vol(I(body_w, cap_w)):.2f} mm3')


if __name__ == '__main__':
    main()
