"""
Generates the Sesame "Aurora head" v200 parts: a round head shell for the Waveshare
ESP32-S3-Touch-AMOLED-1.43C (the cased puck, D50.2 x 14.5 mm) on a ball joint, a top cover
carrying the ball, a socket clamp, and magnet-mounted swappable ears.

Run:   python generate_aurora_head.py [--out DIR] [--base No-Ears.stl] [--tilt DEG] [--ears cat]
Needs: trimesh, manifold3d, shapely, numpy   (pip install trimesh manifold3d shapely)

Body coordinate system (same as the STLs in hardware/printing/stl):
  X = body length, -X is the front (where the ears/OLED used to be)
  Y = up
  Z = body width, the body centre line is at Z = 2.4

Head local frame: z = face axis (+z is the front), y = up, x = sideways. Face at z = +HEAD_T/2.

How the joint works: the ball sits on a short neck on the roof, just behind the head. The head
clamps it in a socket split through the ball centre. The front half is part of the back cap and
the rear half is a separate clamp held by two M2 screws; tightening them sets the friction. The
ball is behind the USB-C plug, so the plug still runs straight down a channel in the head's chin
and the cable drops into the body through a slot in the roof.
"""
import argparse
import os

import numpy as np
import trimesh
from shapely import affinity
from shapely.geometry import Point, Polygon, box
from trimesh.creation import extrude_polygon, revolve

# --------------------------------------------------------------------------- parameters
PUCK_D, PUCK_T = 50.2, 14.5          # Waveshare 1.43C cased board
PUCK_CLR = 0.3                       # radial / axial clearance around it
HEAD_R, HEAD_T = 30.0, 20.0          # shell outer radius and axial thickness
FRONT_FILLET, BACK_FILLET = 6.0, 1.5
BEZEL_WALL = 2.5                     # front wall that retains the puck
BEZEL_OPEN_D = 45.0                  # opening in front of the glass
CAP_T = 2.7                          # back cap disc thickness (sits flush in a counterbore)
CAP_D = 56.0
SCREW_HOLE_D = 1.7                   # M2 self tapping
SCREW_R = 27.7
CAP_SCREW_ANGLES = (90, 210, 330)    # deg, 90 = top. Avoids the chin (270) and side windows (0/180)
SIDE_WIN_W, SIDE_WIN_H = 14.0, 9.0   # button windows at 3 and 9 o'clock (tangential x axial)
VENT_W, VENT_H, VENT_ANGLES = 7.0, 5.0, (245, 295)  # mic / speaker vents beside the chin

# chin: holds the USB-C plug under the puck. The channel is open at the back (the cap's tongue
# closes it) so the puck goes in with the plug already fitted, and open at the bottom for the cable.
CHIN_W, CHIN_T = 21.0, 17.5          # width (x) and thickness (z), flush with the head's back face
CHIN_BOTTOM = -48.0                  # local y of the chin's lower end (the plug ends at -46.1)
CHAN_W, CHAN_FRONT = 15.0, 4.6       # cable channel width and how far forward of the axis it reaches
TONGUE_W = 18.0
PLUG_W, PLUG_T, PLUG_L = 12.5, 7.0, 22.0  # dummy USB-C overmould for the clearance check

# ball joint (head local coordinates of the ball centre)
BALL_R = 9.0
BALL_CLR = 0.05                      # socket radius = BALL_R + BALL_CLR
BALL_Y, BALL_Z = -41.0, -17.5        # behind the plug channel, below the cap disc
SOCKET_R = 12.5                      # outer radius of the socket housing
SOCKET_OPEN_DEG = 58.0               # half angle of the opening under the ball, from straight down
CLAMP_GAP = 0.3                      # the clamp stands off by this much so the screws squeeze the ball
CLAMP_SCREW_X = 14.5                 # two M2 screws at +-x, level with the ball centre
CLAMP_EAR_T = 6.2
NECK_R_TOP, NECK_R_ROOT = 5.0, 6.0
NECK_SCREW_D = 2.5                   # optional M3 self tapping screw up through the neck, from inside
HEAD_ABOVE_ROOF = 53.0               # head centre height above the roof at zero tilt
HEAD_X = 8.0                         # head centre X at zero tilt (the roof starts at X = -3)
DEFAULT_TILT = 8.0                   # face tilted up (deg) in the assembly; adjustable by hand
CABLE_SLOT_W, CABLE_SLOT_L = 12.0, 18.0

# ears: keyed magnet sockets on the rim; each ear set is a separate part
EAR_ANGLES = (38, -38)               # deg from top, positive towards +x
EAR_ZC, EAR_T = -2.25, 11.0          # ears sit on the flat band of the rim between the fillets
KEY_W, KEY_D, KEY_DEPTH = 10.0, 8.0, 1.0   # rim recess: tangential x axial, depth
KEY_CLR = 0.3
MAG_D, MAG_H = 5.2, 2.2              # pocket for a 5 x 2 mm magnet
EAR_KINDS = ('cat', 'bunny', 'bear', 'antenna')

BODY_ZC = 2.4
COVER_STRETCH = 15.0                 # extra body height: the cover is cut at COVER_CUT_Y and a straight band inserted
COVER_CUT_Y = 34.0                   # above the mounting lip and the hip notches, below the old OLED window
ROOF_Y = 55.6 + COVER_STRETCH

V = 'v200'
CAP_FRONT = -HEAD_T / 2 + CAP_T - 0.1    # z of the cap's flat front face


# --------------------------------------------------------------------------- helpers
def U(*ms):
    return trimesh.boolean.union(list(ms), engine='manifold')


def D(a, *ms):
    return trimesh.boolean.difference([a, *ms], engine='manifold')


def I(a, b):
    return trimesh.boolean.intersection([a, b], engine='manifold')


def rot(axis, deg, point=(0, 0, 0)):
    return trimesh.transformations.rotation_matrix(np.radians(deg), axis, point)


def moved(m, t):
    m.apply_translation(t)
    return m


Z_TO_Y = rot([1, 0, 0], -90)             # (x, y, z) -> (x, z, -y): a z range becomes the same y range


def rrect_prism(w, h, r, z0, z1):
    """Rounded rectangle in XY (outer size w x h, corner radius r) extruded from z0 to z1."""
    poly = box(-w / 2 + r, -h / 2 + r, w / 2 - r, h / 2 - r).buffer(r, join_style=1, resolution=24)
    return moved(extrude_polygon(poly, z1 - z0), [0, 0, z0])


def rrect_prism_y(w, d, r, y0, y1):
    """Rounded rectangle in XZ (w along X, d along Z) extruded along Y from y0 to y1."""
    m = rrect_prism(w, d, r, y0, y1)
    m.apply_transform(Z_TO_Y)
    return m


def cyl(r, z0, z1, sections=160):
    return moved(trimesh.creation.cylinder(radius=r, height=z1 - z0, sections=sections), [0, 0, (z0 + z1) / 2])


def ycyl(r, y0, y1, x=0.0, z=0.0, sections=96):
    m = cyl(r, y0, y1, sections)
    m.apply_transform(Z_TO_Y)
    return moved(m, [x, 0, z])


def box_between(x0, x1, y0, y1, z0, z1):
    return moved(trimesh.creation.box(extents=[x1 - x0, y1 - y0, z1 - z0]), [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])


def box_mesh(sx, sy, sz, center=(0, 0, 0)):
    return moved(trimesh.creation.box(extents=[sx, sy, sz]), center)


def sphere(r, c, sub=5):
    return moved(trimesh.creation.icosphere(subdivisions=sub, radius=r), c)


def cone_down(apex, half_deg, h):
    """Solid cone, apex at `apex`, opening downwards (-y) with the given half angle."""
    c = trimesh.creation.cone(radius=h * np.tan(np.radians(half_deg)), height=h, sections=96)  # base z=0, apex z=h
    c.apply_translation([0, 0, -h])          # apex at the origin, base at z = -h
    c.apply_transform(Z_TO_Y)                # base at y = -h
    return moved(c, apex)


def rounded_disc(R, T, r_front, r_back, n=40):
    """Revolved solid: cylinder radius R from z=-T/2 to T/2 with filleted edges (front = +z)."""
    pts = [(0.0, -T / 2)]
    for a in np.linspace(-90, 0, n):
        pts.append((R - r_back + r_back * np.cos(np.radians(a)), -T / 2 + r_back + r_back * np.sin(np.radians(a))))
    for a in np.linspace(0, 90, n):
        pts.append((R - r_front + r_front * np.cos(np.radians(a)), T / 2 - r_front + r_front * np.sin(np.radians(a))))
    pts.append((0.0, T / 2))
    return revolve(np.array(pts), sections=192)


def at_angle(m, r, a_deg):
    return moved(m, [r * np.cos(np.radians(a_deg)), r * np.sin(np.radians(a_deg)), 0])


def on_rim(m, angle_from_top):
    """Rotate a part built pointing straight up (+y) to `angle_from_top` (positive towards +x)."""
    m = m.copy()
    m.apply_transform(rot([0, 0, 1], -angle_from_top))
    return m


# --------------------------------------------------------------------------- ears
def ear_profile(kind):
    """2D outline in the ear's own frame: base on the rim (y = HEAD_R), pointing up +y."""
    base = box(-6.5, HEAD_R - 4, 6.5, HEAD_R + 4)
    if kind == 'cat':
        tri = Polygon([(-11, HEAD_R - 4), (11, HEAD_R - 4), (0, HEAD_R + 15)])
        return tri.buffer(-3.5, join_style=1).buffer(3.5, join_style=1, resolution=24)
    if kind == 'bunny':
        e = affinity.scale(Point(0, 0).buffer(1, resolution=48), 7.0, 18.0)
        e = affinity.rotate(affinity.translate(e, 0, HEAD_R + 15), 12, origin=(0, HEAD_R))  # leans inwards
        return e.union(base).buffer(1.5, join_style=1).buffer(-1.5, join_style=1)
    if kind == 'bear':
        return Point(0, HEAD_R + 3).buffer(9.5, resolution=64).union(base)
    if kind == 'antenna':
        stalk = box(-1.6, HEAD_R - 4, 1.6, HEAD_R + 19)
        tip = Point(0, HEAD_R + 20).buffer(4.5, resolution=48)
        return stalk.union(tip).union(box(-6.5, HEAD_R - 4, 6.5, HEAD_R + 2.5)).buffer(0.8).buffer(-0.8)
    raise ValueError(kind)


def key_recess():
    """Rim recess and magnet pocket in the head, for the socket at the top."""
    r0 = HEAD_R - KEY_DEPTH
    rec = box_between(-KEY_W / 2, KEY_W / 2, r0, HEAD_R + 6, EAR_ZC - KEY_D / 2, EAR_ZC + KEY_D / 2)
    return U(rec, ycyl(MAG_D / 2, r0 - MAG_H, r0 + 0.5, z=EAR_ZC, sections=48))


def build_ear(kind):
    """Ear in head-local coords at the top socket. The base is concave to sit on the rim, a key fits
    the rim recess, and a magnet pocket opens in the key's face."""
    t = 4.0 if kind == 'antenna' else EAR_T
    body = moved(extrude_polygon(ear_profile(kind), t), [0, 0, EAR_ZC - t / 2])
    if kind == 'antenna':                        # thicker base pad so the key and magnet fit
        body = U(body, box_between(-6.5, 6.5, HEAD_R - 4, HEAD_R + 2.5, EAR_ZC - EAR_T / 2, EAR_ZC + EAR_T / 2))
    body = D(body, cyl(HEAD_R, -40, 40, sections=256))
    r0 = HEAD_R - KEY_DEPTH
    key = box_between(-(KEY_W / 2 - KEY_CLR), KEY_W / 2 - KEY_CLR, r0 + 0.1, HEAD_R + 0.6,
                      EAR_ZC - (KEY_D / 2 - KEY_CLR), EAR_ZC + (KEY_D / 2 - KEY_CLR))
    ear = U(body, key)
    return D(ear, ycyl(MAG_D / 2, r0 - 0.5, r0 + 0.1 + MAG_H, z=EAR_ZC, sections=48))


# --------------------------------------------------------------------------- head (local frame)
def chin():
    return moved(rrect_prism_y(CHIN_W, CHIN_T, 3.0, CHIN_BOTTOM, -(HEAD_R - 5.0)), [0, 0, -HEAD_T / 2 + CHIN_T / 2])


def cap_outline(e=0.0):
    """Solid outline of the back cap (grown by e): disc, tongue down the chin, front half of the socket."""
    zb, zf = -HEAD_T / 2, CAP_FRONT
    disc = cyl(CAP_D / 2 + e, zb, zf + e)
    tongue = box_between(-(TONGUE_W / 2 + e), TONGUE_W / 2 + e, CHIN_BOTTOM + 0.5 - e, -(HEAD_R - 8), zb - 0.01, zf + e)
    housing = sphere(SOCKET_R + e, (0, BALL_Y, BALL_Z), sub=4)
    ears = [box_between(11, 18.5 + e, BALL_Y - 4 - e, BALL_Y + 4 + e, BALL_Z - 1, zf + e),
            box_between(-18.5 - e, -11, BALL_Y - 4 - e, BALL_Y + 4 + e, BALL_Z - 1, zf + e)]
    web = box_between(-(9 + e), 9 + e, BALL_Y, -(HEAD_R - 4), BALL_Z + 3.5 - e, zf + e)   # ties the socket to the disc
    sock = I(U(housing, *ears, web), box_between(-40, 40, -80, 0, BALL_Z, zf + e))
    return U(disc, tongue, sock)


def joint_cuts():
    c = (0, BALL_Y, BALL_Z)
    return sphere(BALL_R + BALL_CLR, c), cone_down(c, SOCKET_OPEN_DEG, 40)


def clamp_screws(r, z0, z1):
    return [moved(cyl(r, z0, z1, sections=32), [sx * CLAMP_SCREW_X, BALL_Y, 0]) for sx in (1, -1)]


def build_head_local():
    zf = HEAD_T / 2
    shell = U(rounded_disc(HEAD_R, HEAD_T, FRONT_FILLET, BACK_FILLET), chin())

    pocket_r = PUCK_D / 2 + PUCK_CLR
    pocket_front = zf - BEZEL_WALL
    cuts = [cyl(pocket_r, -zf - 1, pocket_front),                 # puck pocket, open at the back
            cyl(BEZEL_OPEN_D / 2, pocket_front - 1, zf + 1)]      # bezel opening
    cone = trimesh.creation.cone(radius=BEZEL_OPEN_D / 2 + 1.5 + 20, height=20 + 1.5, sections=160)
    cone.apply_translation([0, 0, zf - 1.5])
    cuts.append(I(cone, cyl(BEZEL_OPEN_D / 2 + 1.5, zf - 1.5, zf + 1)))   # chamfer D45 -> D48
    cuts.append(box_between(-CHAN_W / 2, CHAN_W / 2, CHIN_BOTTOM - 2, -(pocket_r - 3), -zf - 1, CHAN_FRONT))  # cable channel
    cuts.append(cap_outline(0.2))                                 # counterbore for the cap disc, tongue and socket half
    for sgn in (1, -1):                                          # button windows, both sides
        cuts.append(box_mesh(14, SIDE_WIN_W, SIDE_WIN_H, center=(sgn * HEAD_R, 0, 0)))
    for a in VENT_ANGLES:                                         # mic / speaker vents
        v = box_mesh(VENT_W, 14, VENT_H, center=(0, HEAD_R, 0))
        v.apply_transform(rot([0, 0, 1], a - 90))
        cuts.append(v)
    for a in CAP_SCREW_ANGLES:
        cuts.append(at_angle(cyl(SCREW_HOLE_D / 2, -zf - 1, -zf + CAP_T + 6.0, sections=32), SCREW_R, a))
    for a in EAR_ANGLES:
        cuts.append(on_rim(key_recess(), a))
    body = D(shell, *cuts)

    cav, opening = joint_cuts()
    cap_cuts = [cav, opening, *clamp_screws(SCREW_HOLE_D / 2, BALL_Z - 1, BALL_Z + 9.0)]
    for a in CAP_SCREW_ANGLES:
        cap_cuts += [at_angle(cyl(1.15, -zf - 1, -zf + CAP_T + 1, sections=32), SCREW_R, a),
                     at_angle(cyl(2.1, -zf - 1, -zf + 1.0, sections=32), SCREW_R, a)]
    cap = D(cap_outline(0.0), *cap_cuts)

    zc = BALL_Z - CLAMP_GAP                                       # clamp front face
    housing = sphere(SOCKET_R, (0, BALL_Y, BALL_Z), sub=4)
    ears = [box_between(11, 18.5, BALL_Y - 4, BALL_Y + 4, zc - CLAMP_EAR_T, zc + 1),
            box_between(-18.5, -11, BALL_Y - 4, BALL_Y + 4, zc - CLAMP_EAR_T, zc + 1)]
    clamp = I(U(housing, *ears), box_between(-40, 40, -80, 0, BALL_Z - 30, zc))
    clamp = D(clamp, cav, opening, *clamp_screws(1.15, zc - 20, zc + 1),
              *clamp_screws(2.1, zc - 20, zc - CLAMP_EAR_T + 1.6))
    return body, cap, clamp


def ball_world():
    return np.array([HEAD_X - BALL_Z, ROOF_Y + HEAD_ABOVE_ROOF + BALL_Y, BODY_ZC])


def head_world_transform(tilt=DEFAULT_TILT, pan=0.0):
    """local -> world: x->Z, y->Y, z->-X with the ball centre on the neck, then tilt/pan about the ball."""
    M = np.eye(4)
    M[:3, :3] = np.array([[0, 0, -1], [0, 1, 0], [1, 0, 0]])
    bw = ball_world()
    M[:3, 3] = bw - M[:3, :3] @ np.array([0, BALL_Y, BALL_Z])
    return rot([0, 1, 0], pan, point=bw) @ rot([0, 0, 1], -tilt, point=bw) @ M


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


def fill_oled_window(cov):
    """Sample the front face around the old OLED window, fit a plane and close the hole with a slab."""
    S = COVER_STRETCH
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
        n = -n
    win = (np.isnan(hx) | (hx > -3.5)) & (Y > 34 + S) & (Y < 51 + S) & (Z > -11) & (Z < 16)
    wy0, wy1 = Y[win].min() - 1.5, Y[win].max() + 1.5
    wz0, wz1 = Z[win].min() - 1.5, Z[win].max() + 1.5
    up = np.array([0, 1.0, 0])
    up -= (up @ n) * n
    up /= np.linalg.norm(up)
    side = np.cross(n, up)
    cy, cz = (wy0 + wy1) / 2, (wz0 + wz1) / 2
    cx = c[0] - (n[1] * (cy - c[1]) + n[2] * (cz - c[2])) / n[0]
    slab = box_mesh(wz1 - wz0, wy1 - wy0, 1.6)
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2] = side, up, n
    M[:3, 3] = np.array([cx, cy, cz]) - n * 0.85
    slab.apply_transform(M)
    return U(cov, slab), (wy0, wy1, wz0, wz1)


def roof_height(cov, x, z):
    loc, _, _ = cov.ray.intersects_location([[x, 200, z]], [[0, -1.0, 0]], multiple_hits=False)
    return float(loc[0][1]) if len(loc) else float('nan')


def cable_exit_world(tilt=DEFAULT_TILT):
    """Where the cable leaves the bottom of the chin channel, in body coordinates."""
    p = np.array([0, CHIN_BOTTOM, (CAP_FRONT + CHAN_FRONT) / 2, 1.0])
    return (head_world_transform(tilt) @ p)[:3]


def build_cover(base_path):
    cov = stretch_cover(trimesh.load(base_path), COVER_CUT_Y, COVER_STRETCH)
    cov, win = fill_oled_window(cov)
    bw = ball_world()
    roof_at_ball = roof_height(cov, bw[0], bw[2])
    rings = [ycyl(NECK_R_ROOT, ROOF_Y - 1.5, ROOF_Y - 1.49, x=bw[0], z=bw[2]),
             ycyl(NECK_R_TOP, bw[1] - BALL_R + 2.0, bw[1] - BALL_R + 2.01, x=bw[0], z=bw[2])]
    neck = trimesh.convex.convex_hull(np.vstack([r.vertices for r in rings]))
    boss = ycyl(5.5, ROOF_Y - 6.0, ROOF_Y - 0.5, x=bw[0], z=bw[2])          # meat under the roof for the neck screw
    cov = U(cov, neck, sphere(BALL_R, bw), boss)
    ce = cable_exit_world()
    slot = moved(rrect_prism_y(CABLE_SLOT_L, CABLE_SLOT_W, CABLE_SLOT_W / 2 - 0.01, ROOF_Y - 8, ROOF_Y + 3), [ce[0], 0, BODY_ZC])
    pilot = ycyl(NECK_SCREW_D / 2, ROOF_Y - 6.5, bw[1] + 4, x=bw[0], z=bw[2], sections=32)
    return D(cov, slot, pilot), win, roof_at_ball, ce


# --------------------------------------------------------------------------- assembly helpers
def puck_local():
    zc = HEAD_T / 2 - BEZEL_WALL - PUCK_CLR / 2 - PUCK_T / 2
    p = cyl(PUCK_D / 2, zc - PUCK_T / 2, zc + PUCK_T / 2)
    plug = box_mesh(PLUG_W, PLUG_L, PLUG_T, center=(0, -PUCK_D / 2 - PLUG_L / 2 + 1.0, zc))  # 1 mm into the port
    return U(p, plug)


def world(m, W):
    m = m.copy()
    m.apply_transform(W)
    return m


def flat_back_down(m):
    """Head body and ears: local -z face on the bed."""
    p = m.copy()
    p.apply_translation([0, 0, -p.bounds[0][2]])
    return p


def flat_front_down(m):
    """Cap and clamp: their flat front face on the bed."""
    p = m.copy()
    p.apply_transform(rot([0, 1, 0], 180))
    p.apply_translation([0, 0, -p.bounds[0][2]])
    return p


def placed_ears(ears, kind, W):
    out = []
    if kind == 'none':
        return out
    for ang in (EAR_ANGLES if kind != 'antenna' else EAR_ANGLES[1:]):
        e = ears[kind]
        if ang < 0:                                  # left ear: the same part, flipped over
            e = e.copy()
            e.apply_transform(rot([0, 1, 0], 180, point=[0, 0, EAR_ZC]))
        out.append(world(on_rim(e, ang), W))
    return out


def vol(m):
    return m.volume if len(m.faces) else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=None)
    ap.add_argument('--base', default=None)
    ap.add_argument('--tilt', type=float, default=DEFAULT_TILT, help='tilt shown in the assembly GLB')
    ap.add_argument('--ears', default='cat', choices=EAR_KINDS + ('none',), help='ears shown in the assembly GLB')
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
    covers = os.path.join(repo, 'hardware', 'printing', 'stl', 'top-covers')
    base = a.base or os.path.join(covers, 'Top-Cover-No-Ears-v100.stl')
    out = a.out or os.path.join(covers, 'aurora-head')
    os.makedirs(out, exist_ok=True)

    body, cap, clamp = build_head_local()
    cover, win, roof_at_ball, ce = build_cover(base)
    ears = {k: build_ear(k) for k in EAR_KINDS}
    puck = puck_local()

    files = {
        f'Top-Cover-Aurora-{V}.stl': cover,
        f'Aurora-Head-Body-{V}.stl': flat_back_down(body),
        f'Aurora-Head-Back-Cap-{V}.stl': flat_front_down(cap),
        f'Aurora-Head-Socket-Clamp-{V}.stl': flat_front_down(clamp),
    }
    for k, e in ears.items():
        files[f'Aurora-Ear-{k.capitalize()}-{V}.stl'] = flat_back_down(e)
    for f, m in files.items():
        m.export(os.path.join(out, f))

    W = head_world_transform(a.tilt)
    scene = {'cover': cover, 'head': world(body, W), 'cap': world(cap, W), 'clamp': world(clamp, W), 'puck': world(puck, W)}
    for i, e in enumerate(placed_ears(ears, a.ears, W)):
        scene[f'ear{i}'] = e
    trimesh.Scene(scene).export(os.path.join(out, f'Aurora-Head-Assembly-{V}.glb'))

    # ---------------------------------------------------------------- checks
    for f in files:
        r = trimesh.load(os.path.join(out, f))
        print(f'{f}: reloaded watertight={r.is_watertight} volume={r.volume:.0f} mm3 size={np.round(r.extents, 1).tolist()}')
    print('filled window Y %.1f..%.1f  Z %.1f..%.1f' % win)
    print(f'ball centre {np.round(ball_world(), 1).tolist()}, roof under it at Y {roof_at_ball:.2f} (expected {ROOF_Y:.2f})')
    print(f'cable exit at default tilt {np.round(ce, 1).tolist()}')
    print('puck+plug overlap: body %.2f  cap %.2f  clamp %.2f mm3' % (vol(I(body, puck)), vol(I(cap, puck)), vol(I(clamp, puck))))
    print('part overlaps: body/cap %.2f  body/clamp %.2f  cap/clamp %.2f mm3' % (vol(I(body, cap)), vol(I(body, clamp)), vol(I(cap, clamp))))
    for k, e in ears.items():
        print(f'ear {k}: overlap with head body {vol(I(on_rim(e, EAR_ANGLES[0]), body)):.2f} mm3')
    head_parts = U(body, cap, clamp)
    for tilt in (-10, 0, DEFAULT_TILT, 24):
        for pan in (0, 25, -25):
            Wt = head_world_transform(tilt, pan)
            hp = world(head_parts, Wt)
            print(f'tilt {tilt:+5.1f} pan {pan:+3.0f}: head vs cover {vol(I(hp, cover)):6.2f} mm3, '
                  f'puck+plug vs cover {vol(I(world(puck, Wt), cover)):6.2f} mm3, lowest head point Y {hp.bounds[0][1]:.1f}')


if __name__ == '__main__':
    main()
