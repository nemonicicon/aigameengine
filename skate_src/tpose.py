#!/usr/bin/env python3
"""tpose.py -- put ANY skinned body into a T-POSE and bake it there (2026-10-05).

jacob: "what we are trying to do here is to get t posed meshes." A .brres or a .dae arrives in
whatever pose its game or its artist left it in -- an A-pose, a fighting stance, a Wii
character with its arms at 45 degrees -- and every rigging and retargeting tool downstream
(statue2body, rig_reva, autoweights, retarget_skate) wants a T-pose: arms along +-X, legs
straight down, spine up, facing +Z. This module is the step between an importer's read and
its write:

    RM   = canon_map(nodes, names, override)        which node is which limb (names, then --map)
    qov  = solve_tpose(nodes, RM, report)           a local q per bone that straightens it
    bake_tpose(nodes, meshes, clips, qov)           the skin re-baked IN the T-pose: positions,
                                                    normals, inverse binds, the rest pose itself
    measure(nodes, RM)                              the receipt: every limb's angle off its axis

The solve is aim-based, parent-first, with the authoring kit's own primitives (authorlib
aim_bone: shortest arc, so the roll of a bone changes as little as it can). Each limb is aimed
at the direction a T-pose gives it, the side taken from where the hand or foot actually IS, not
from the name. Since Kanden (10-05, round 2): the pelvis is first SQUARED to the hip line (a
statue's stance is turned); a bent elbow or knee straightens with its ROLL set (elbow points
back, kneecaps forward, the same on both sides); a hand is aimed at its End node; a foot keeps
its statue pitch (flat) and turns to face forward; a HeadFront node turns the face forward; and
the tool lifts the pelvis so the lowest vertex stands where the statue's stood. Baking: new world matrices W' from the posed locals; a vertex goes through its
old palette into rig space and comes back out of the new one, so the body does not move -- its
REST moves to meet it. Inverse binds become inv(W'), the bind matrix identity, and every clip
that lacked a rotation track for a bone the solve moved gets one holding the OLD rest, so an
imported animation still plays as authored.

The scene shape is rpackwrite's: nodes [{name,parent,kind,t,q,s}] parent-first, meshes
[{pos,nrm,uv,idx,joints,weights,boneMap,ibm,bind,...}], clips [(name,fps,frames,{(node,type):floats})].

As a tool:  tpose.py <pack.rpack> <out.rpack> [--map pelvis=Hip,upperarm_l=ArmL1,...] [--bones]
                     [--symmetric] [--proof out.png] [--dae out.dae] [--glb out.glb]
            puts a finished pack's body into a T-pose (append-only is NOT possible here: the
            vertex buffers change, so it writes a new pack through rpackwrite), and the same body
            as COLLADA (PNGs beside it) and/or binary glTF for Blender, Godot, anything.
"""
import sys, os, math, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'author'))
import rpackwrite as RW
from authorlib import fk_world, aim_bone, rotate_bone_world, qmul, qinv, qnorm, qrot, q_between, q_axis_angle
import retarget

CANON_KEYS = ['pelvis', 'spine_01', 'spine_02', 'spine_03', 'neck_01', 'head',
              'clavicle_l', 'upperarm_l', 'lowerarm_l', 'clavicle_r', 'upperarm_r', 'lowerarm_r',
              'thigh_l', 'calf_l', 'thigh_r', 'calf_r']
EXTRA_KEYS = ['hand_l', 'hand_r', 'foot_l', 'foot_r', 'ball_l', 'ball_r']


# ----------------------------------------------------------------- which bone is which
def _side_and_part(name):
    """Nintendo and generic families the club's canon does not know. Returns (canon, side) or None.
    Side 'l'/'r'/'' ; canon one of CANON_KEYS/EXTRA_KEYS without its side suffix."""
    n = name
    lo = n.lower()
    side = ''
    # side: Left/Right words, l_/r_ or _l/_r, a leading L/R capital (Smash: LArmN), a trailing L/R (Galaxy: ArmL1)
    if 'left' in lo: side = 'l'
    elif 'right' in lo: side = 'r'
    elif re.match(r'^[lr][_A-Z]', n): side = n[0].lower()
    elif re.search(r'[_.]([lr])(\d*)$', lo): side = re.search(r'[_.]([lr])(\d*)$', lo).group(1)
    elif re.search(r'[a-z]([LR])(\d*)$', n): side = re.search(r'[a-z]([LR])(\d*)$', n).group(1).lower()
    elif re.search(r'^([LR])[A-Z]', n): side = n[0].lower()
    core = re.sub(r'(left|right|^[lr]_|_[lr]$|_[lr](?=\d)|^[lr](?=[A-Z]))', '', n if not lo.startswith(('left', 'right')) else lo)
    core = re.sub(r'[LR](?=\d*$)', '', core) if side and re.search(r'[a-z][LR]\d*$', n) else core
    c = core.lower().strip('_ ')
    c = re.sub(r'[nj]$', '', c) if re.search(r'[a-z][nj]$', c) and c[:-1] in (
        'hip', 'waist', 'bust', 'neck', 'head', 'shoulder', 'arm', 'hand', 'leg', 'knee', 'foot', 'toe', 'clavicle') else c
    num = re.search(r'(\d+)$', c)
    k = int(num.group(1)) if num else 0
    c = re.sub(r'\d+$', '', c).strip('_')
    if c in ('hip', 'hips', 'pelvis', 'waist' if k == 0 and not side else '_'):
        return ('pelvis', '') if c != 'waist' else ('spine_01', '')
    if c in ('waist',): return ('spine_01', '')
    if c in ('bust', 'chest', 'spine', 'spin', 'body', 'torso'):
        return ({0: 'spine_01', 1: 'spine_01', 2: 'spine_02', 3: 'spine_03'}.get(k, 'spine_03'), '')
    if c in ('neck',): return ('neck_01', '')
    if c in ('head',): return ('head', '')
    if not side: return None
    if c in ('shoulder', 'clavicle', 'collar'): return ('clavicle', side)
    if c in ('arm', 'upperarm', 'uparm'): return ({0: 'upperarm', 1: 'upperarm', 2: 'lowerarm'}.get(k, 'lowerarm'), side)
    if c in ('forearm', 'lowerarm', 'elbow'): return ('lowerarm', side)
    if c in ('hand', 'wrist'): return ('hand', side)
    if c in ('leg', 'upleg', 'thigh'): return ({0: 'thigh', 1: 'thigh', 2: 'calf', 3: 'foot'}.get(k, 'calf'), side)
    if c in ('knee', 'calf', 'shin', 'lowleg'): return ('calf', side)
    if c in ('foot', 'ankle'): return ('foot', side)
    if c in ('toe', 'toebase', 'ball'): return ('ball', side)
    return None


def canon_map(nodes, names, override=None, report=print):
    """{'pelvis': node, 'upperarm_l': node, ...}; -1 where nothing resolved. The club's canon
    (retarget.canon_of) first, the Nintendo/generic matcher second, --map overrides last."""
    m = {k: -1 for k in CANON_KEYS + EXTRA_KEYS}
    for i, nm in enumerate(names):
        c = retarget.canon_of(nm)
        if c >= 0 and m[CANON_KEYS[c]] < 0:
            m[CANON_KEYS[c]] = i
    for i, nm in enumerate(names):
        sp = _side_and_part(nm)
        if not sp:
            continue
        part, side = sp
        key = part + ('_' + side if side else '')
        if key in m and m[key] < 0:
            m[key] = i
    # the spine chain by topology, like retarget.resolve: whatever lies between neck and pelvis
    if m['pelvis'] >= 0 and m['neck_01'] >= 0:
        chain, a = [], nodes[m['neck_01']]['parent']
        while a >= 0 and a != m['pelvis']:
            chain.append(a)
            a = nodes[a]['parent']
        if a == m['pelvis']:
            chain.reverse()
            for k in range(3):
                m['spine_0%d' % (k + 1)] = chain[k] if k < len(chain) else -1
            if len(chain) > 3:
                m['spine_03'] = chain[-1]
    for k, v in (override or {}).items():
        if isinstance(v, str):
            if v not in names:
                raise SystemExit('--map %s=%s: no node of that name (use --bones to list them)' % (k, v))
            v = names.index(v)
        m[k] = v
    # chain ends from topology where names gave none
    for s in 'lr':
        if m['hand_' + s] < 0 and m['lowerarm_' + s] >= 0:
            m['hand_' + s] = _far_child(nodes, names, m['lowerarm_' + s], 'hand')
        if m['foot_' + s] < 0 and m['calf_' + s] >= 0:
            m['foot_' + s] = _far_child(nodes, names, m['calf_' + s], 'foot')
        if m['ball_' + s] < 0 and m['foot_' + s] >= 0:
            m['ball_' + s] = _far_child(nodes, names, m['foot_' + s], 'toe')
    missing = [k for k in CANON_KEYS + EXTRA_KEYS if m[k] < 0]
    if report:
        report('  canon: ' + ', '.join('%s=%s' % (k, names[m[k]]) for k in CANON_KEYS + EXTRA_KEYS if m[k] >= 0))
        if missing:
            report('  canon: UNRESOLVED %s  (--map key=NodeName; --bones lists the rig)' % ' '.join(missing))
    m['_missing'] = missing
    return m


def _far_child(nodes, names, node, word):
    best, bd = -1, -1.0
    for i, nd in enumerate(nodes):
        if nd['parent'] != node:
            continue
        d = sum(x * x for x in nd['t']) + (1e12 if word in names[i].lower() else 0.0)
        if d > bd:
            bd, best = d, i
    return best


def print_bones(nodes, names):
    wp, _ = fk_world(nodes)
    for i, nd in enumerate(nodes):
        depth, a = 0, nd['parent']
        while a >= 0:
            depth += 1
            a = nodes[a]['parent']
        print('  %3d %s%-28s kind %d  world (%7.3f %7.3f %7.3f)' % (i, '  ' * depth, names[i][:28], nd.get('kind', 0), wp[i][0], wp[i][1], wp[i][2]))


# ----------------------------------------------------------------- the solve
def _dir(a, b):
    return [b[k] - a[k] for k in range(3)]


def _len(v):
    return math.sqrt(sum(x * x for x in v))


def _unit(v):
    L = _len(v) or 1.0
    return [x / L for x in v]


def _dot(a, b):
    return sum(a[k] * b[k] for k in range(3))


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _perp(v, axis):
    """the part of v perpendicular to the unit axis"""
    d = _dot(v, axis)
    return [v[k] - d * axis[k] for k in range(3)]


def _bend(nodes, qov, a, b, c):
    """(bend angle in degrees, the joint's convex-side direction) of the chain a-b-c as posed now: for a
    knee the convex side is the kneecap, for an elbow the point of the elbow"""
    wp, _ = fk_world(nodes, qov)
    u, f = _unit(_dir(wp[a], wp[b])), _unit(_dir(wp[b], wp[c]))
    ang = math.degrees(math.acos(max(-1.0, min(1.0, _dot(u, f)))))
    return ang, _unit(_perp([u[k] - f[k] for k in range(3)], u))


def aim_pole(nodes, bone, child, target_dir, pole_now, pole_target, q_override=None):
    """aim_bone with the ROLL decided: a new local q for `bone` so bone->child points along target_dir
    AND the direction pole_now (a world direction riding on the bone, e.g. a kneecap) ends up as
    close to pole_target as the aim allows. Shortest arc first, then the twist about the new axis."""
    wp, wq = fk_world(nodes, q_override)
    p = nodes[bone]["parent"]
    rp = wq[p] if p >= 0 else [0, 0, 0, 1]
    cur = _dir(wp[bone], wp[child])
    A1 = q_between(cur, target_dir)
    t = _unit(target_dir)
    a = _perp(qrot(A1, pole_now), t)
    b = _perp(pole_target, t)
    if _len(a) < 1e-6 or _len(b) < 1e-6:
        A = A1
    else:
        a, b = _unit(a), _unit(b)
        ang = math.atan2(_dot(_cross(a, b), t), _dot(a, b))
        A = qmul(q_axis_angle(t, ang), A1)
    rl = (q_override or {}).get(bone, nodes[bone]["q"])
    return qnorm(qmul(qinv(rp), qmul(A, qmul(rp, rl))))


def solve_tpose(nodes, RM, facing=None, report=print):
    """Return {node: local q} posing the resolved limbs into a T-pose. facing: the world axis the
    body faces, auto-detected from the toes when None. Bones the map lacks are left alone, loudly."""
    qov = {}
    names = [nd['name'] for nd in nodes]
    wp, wq = fk_world(nodes)
    up = [0.0, 1.0, 0.0]
    # facing from the feet (toe ahead of ankle), else from the arms' plane, else +z
    fz = None
    for s in 'lr':
        f, b = RM.get('foot_' + s, -1), RM.get('ball_' + s, -1)
        if f >= 0 and b >= 0 and b != f:
            d = _dir(wp[f], wp[b])
            if abs(d[2]) > abs(d[0]):
                fz = 1.0 if d[2] > 0 else -1.0
            break
    if facing in ('+z', 'z'): fz = 1.0
    elif facing == '-z': fz = -1.0
    if fz is None:
        fz = 1.0
        if report: report('  facing: no toe to read -- assuming +z')
    else:
        if report: report('  facing: %sz (from the feet)' % ('+' if fz > 0 else '-'))
    fwd = [0.0, 0.0, fz]

    def aim(bone, child, target):
        if bone < 0 or child < 0 or bone == child:
            return
        qov[bone] = aim_bone(nodes, bone, child, target, qov)

    def aim_limb(bone, child, tip, target, pole_target, what):
        """aim bone->child at target; when the joint at `child` is bent (> 15 deg), with the ROLL
        set so the joint's convex side faces pole_target. Shortest-arc aims leave each limb's roll
        to its stance: Kanden's two arms came out of one statue rolled 45 deg apart (10-05)."""
        if bone < 0 or child < 0 or bone == child:
            return
        if tip >= 0 and tip != child:
            ang, pole = _bend(nodes, qov, bone, child, tip)
            if ang > 15.0:
                qov[bone] = aim_pole(nodes, bone, child, target, pole, pole_target, qov)
                if report:
                    report('  %s: bent %.0f deg, its %s turned to %s' % (names[bone], ang, what, pole_target))
                return
        aim(bone, child, target)

    # 0. SQUARE THE BODY (10-05, Kanden round 2): a statue mid-stance is turned -- Kanden's torso
    #    15 deg about y and 2.6 deg over, his hips with it -- and a T-pose that straightens limbs on a
    #    turned body puts one arm and one leg ahead of the other. The pelvis turns so the hip line
    #    (right hip -> left hip) lies along x; everything rides it. Only a TURN is corrected (< 45 deg):
    #    a body facing the other way is the importers' --yaw, never this
    pel, tl, tr = RM.get('pelvis', -1), RM.get('thigh_l', -1), RM.get('thigh_r', -1)
    if pel >= 0 and tl >= 0 and tr >= 0:
        h = _unit(_dir(wp[tr], wp[tl]))
        want = [fz, 0.0, 0.0]               # his left is +x when he faces +z
        off = math.degrees(math.acos(max(-1.0, min(1.0, _dot(h, want)))))
        if 0.5 < off < 45.0:
            p = nodes[pel]['parent']
            rp = wq[p] if p >= 0 else [0, 0, 0, 1]
            qov[pel] = qnorm(qmul(qinv(rp), qmul(q_between(h, want), qmul(rp, nodes[pel]['q']))))
            if report:
                report('  square: the hip line was %.1f deg off x (turned %+.1f, tilted %+.1f) -- the pelvis squared to it'
                       % (off, math.degrees(math.atan2(h[2], h[0])), math.degrees(math.asin(max(-1.0, min(1.0, h[1]))))))
        elif report and off >= 45.0:
            report('  square: the hip line is %.0f deg off x -- NOT a turn; left alone (wrong facing? --yaw)' % off)

    # 1. the spine, up: each spine bone aims at the next, the last at the neck, the neck at the head
    sp = [RM[k] for k in ('spine_01', 'spine_02', 'spine_03') if RM[k] >= 0]
    chain = sp + ([RM['neck_01']] if RM['neck_01'] >= 0 else []) + ([RM['head']] if RM['head'] >= 0 else [])
    for a, b in zip(chain, chain[1:]):
        aim(a, b, up)
    # 1b. the FACE forward: a head turned on its body stays turned when the body is squared (Kanden's
    #     faced +z on a torso turned 15 deg; squared, his face looked 18 deg to his left). A HeadFront
    #     node (statue2body writes one from the mark) says where the face points; the head turns about
    #     the vertical until it points along the facing. No HeadFront: the head keeps its turn.
    head = RM['head']
    front = next((i for i, nd in enumerate(nodes) if nd['parent'] == head and 'front' in nd['name'].lower()), -1) if head >= 0 else -1
    if front >= 0:
        wp2, _ = fk_world(nodes, qov)
        f = _dir(wp2[head], wp2[front])
        turn = math.atan2(0.0, fz) - math.atan2(f[0], f[2])
        turn = math.atan2(math.sin(turn), math.cos(turn))
        if abs(math.degrees(turn)) < 60.0:
            qov[head] = rotate_bone_world(nodes, head, [0.0, 1.0, 0.0], turn, qov)
            if report:
                report('  %s: the face looked %+.0f deg off the facing -- turned to face %sz' % (names[head], -math.degrees(turn), '+' if fz > 0 else '-'))
    # 2. the arms: the side is where the hand IS (names lie, mirrored exports exist)
    for s in 'lr':
        ua, la, hd, cl = RM['upperarm_' + s], RM['lowerarm_' + s], RM['hand_' + s], RM['clavicle_' + s]
        if ua < 0:
            continue
        probe = hd if hd >= 0 else (la if la >= 0 else ua)
        wp2, _ = fk_world(nodes, qov)
        sx = 1.0 if wp2[probe][0] - wp2[RM['pelvis'] if RM['pelvis'] >= 0 else 0][0] >= 0 else -1.0
        out = [sx, 0.0, 0.0]
        if cl >= 0:
            # the clavicle straight out to the side too: a twisted stance pulls one shoulder back
            # (Noxus, 10-05) and a T-pose with one arm behind the other is not a T-pose
            aim(cl, ua, out)
        # the elbow's point faces BACK: arms out, palms down -- the same roll on both sides
        aim_limb(ua, la, hd, out, [0.0, 0.0, -fz], 'elbow point')
        aim(la, hd, out)
        if hd >= 0:
            # the hand along the forearm: aimed at its END NODE (the fist's far side, a cannon's tip)
            tip = _far_child(nodes, names, hd, 'end')
            if tip >= 0:
                aim(hd, tip, out)
            elif report:
                report('  %s: no end node -- the hand keeps its bend (rig it with a HandEnd)' % names[hd])
    # 3. the legs, straight down, the kneecaps forward; the foot forward and FLAT
    for s in 'lr':
        th, ca, ft, bl = RM['thigh_' + s], RM['calf_' + s], RM['foot_' + s], RM['ball_' + s]
        if th < 0:
            continue
        aim_limb(th, ca, ft, [0.0, -1.0, 0.0], [0.0, 0.0, fz], 'kneecap')
        aim(ca, ft, [0.0, -1.0, 0.0])
        if ft >= 0 and bl >= 0 and bl != ft:
            wp2, wq2 = fk_world(nodes, qov)
            d = _dir(wp2[ft], wp2[bl])
            shin = _len(_dir(wp2[ca], wp2[ft])) if ca >= 0 else 0.0
            if _len(d) > 0.5 * shin and shin > 0:
                # DIGITIGRADE (Noxus, 10-05): the ankle is raised and the foot bone is as long as the
                # shin -- a metatarsus. Straight down, like the rest of the leg: the creature stands
                # tall on its toes, and the toe bone below carries the forward direction
                aim(ft, bl, [0.0, -1.0, 0.0])
                tip = _far_child(nodes, names, bl, 'end')
                if tip >= 0 and tip != bl:
                    aim(bl, tip, [0.0, -0.3, fz])
            else:
                # ⛔ the old aim kept the drop the foot had AFTER the shin was straightened (Kanden's left
                # shin leaned 30 deg: the foot came out pitched by it) and dropped the sideways part of
                # a splayed foot from its length (steeper still). The foot keeps its STATUE orientation
                # in the world -- the sole lay flat on the ground there -- turned about y to face forward
                d0 = _dir(wp[ft], wp[bl])
                turn = math.atan2(0.0, fz) - math.atan2(d0[0], d0[2])
                want = qmul(q_axis_angle([0.0, 1.0, 0.0], turn), wq[ft])
                p = nodes[ft]['parent']
                qov[ft] = qnorm(qmul(qinv(wq2[p]) if p >= 0 else [0, 0, 0, 1], want))
                if report:
                    report('  %s: turned %+.0f deg to face %sz, its statue pitch kept (sole flat)' % (names[ft], math.degrees(turn), '+' if fz > 0 else '-'))
    if report:
        report('  tpose: %d bones re-aimed' % len(qov))
    return qov


def symmetrise(nodes, RM, report=print):
    """Equal LIMB LENGTHS left and right: each limb child's local translation is rescaled to the mean
    of the two sides (upper arm -> forearm, forearm -> hand, thigh -> calf, calf -> foot). Hand-measured
    marks put the two sides a few centimetres apart and the T-pose read as a stance (10-05). Returns
    {node: t} overrides for bake_tpose."""
    tov = {}
    for (par, child) in (('clavicle', 'upperarm'), ('upperarm', 'lowerarm'), ('lowerarm', 'hand'), ('thigh', 'calf'), ('calf', 'foot'), ('foot', 'ball')):
        a, b = RM.get(child + '_l', -1), RM.get(child + '_r', -1)
        pa, pb = RM.get(par + '_l', -1), RM.get(par + '_r', -1)
        if min(a, b, pa, pb) < 0 or nodes[a]['parent'] != pa or nodes[b]['parent'] != pb:
            continue
        la, lb = _len(nodes[a]['t']), _len(nodes[b]['t'])
        if la < 1e-9 or lb < 1e-9:
            continue
        mean = 0.5 * (la + lb)
        tov[a] = [v * mean / la for v in nodes[a]['t']]
        tov[b] = [v * mean / lb for v in nodes[b]['t']]
        if report and abs(la - lb) > 1e-6:
            report('  symmetric: %s %.3f / %.3f -> %.3f' % (child, la, lb, mean))
    return tov


def ground_lift(nodes, RM, qov, tov, report=print, meshes=None):
    """Straight legs are longer than bent ones: Kanden's T-pose put his soles 0.24 BELOW the floor he
    stood on (10-05). The pelvis is lifted so the body's LOWEST VERTEX stands where the statue's stood
    (meshes given: the posed skin is measured), else so the lowest joint of each foot chain (ankle,
    ball, toe end) does -- ⛔ joints alone left Noxus's claws 0.28 under the floor, his mesh reaches
    below his toe joints. Returns tov with the pelvis entry set."""
    tov = dict(tov or {})
    pel = RM.get('pelvis', -1)
    if pel < 0:
        return tov
    names = [nd['name'] for nd in nodes]
    wp0, _ = fk_world(nodes)
    wp1, wq1 = fk_world(nodes, qov, tov)
    drops = []
    if meshes:
        import proofrender
        posed = [dict(nd, q=list(qov.get(i, nd['q'])), t=list(tov.get(i, nd['t']))) for i, nd in enumerate(nodes)]
        drops.append(float(proofrender.skinned(nodes, meshes)[0][:, 1].min() - proofrender.skinned(posed, meshes)[0][:, 1].min()))
    else:
        for s in 'lr':
            chain = [RM.get(k + '_' + s, -1) for k in ('foot', 'ball')]
            if chain[-1] >= 0:
                chain.append(_far_child(nodes, names, chain[-1], 'end'))
            chain = [c for c in chain if c >= 0]
            if chain:
                drops.append(min(wp0[c][1] for c in chain) - min(wp1[c][1] for c in chain))
    if not drops or max(drops) < 1e-4:
        return tov
    dy = max(drops)
    p = nodes[pel]['parent']
    loc = qrot(qinv(wq1[p]), [0.0, dy, 0.0]) if p >= 0 else [0.0, dy, 0.0]
    t = list(tov.get(pel, nodes[pel]['t']))
    tov[pel] = [t[k] + loc[k] for k in range(3)]
    if report:
        report('  ground: straight legs are longer -- the pelvis lifted %.3f so the %s stands where it stood'
               % (dy, 'lowest vertex' if meshes else 'lowest foot joint'))
    return tov


def measure(nodes, RM, qov=None, report=print):
    """The receipt: each limb's angle off its T-pose axis, in degrees. Returns {bone key: deg}."""
    wp, _ = fk_world(nodes, qov)
    out = {}

    def ang(a, b, axis):
        if a < 0 or b < 0 or a == b:
            return None
        d = _dir(wp[a], wp[b])
        L = _len(d)
        if L < 1e-9:
            return None
        c = sum(d[k] * axis[k] for k in range(3)) / L
        return math.degrees(math.acos(max(-1.0, min(1.0, c))))
    for s in 'lr':
        sx = 1.0 if (RM['hand_' + s] >= 0 and wp[RM['hand_' + s]][0] >= wp[RM['pelvis']][0]) or (RM['hand_' + s] < 0 and s == 'l') else -1.0
        out['clavicle_' + s] = ang(RM['clavicle_' + s], RM['upperarm_' + s], [sx, 0, 0])
        out['upperarm_' + s] = ang(RM['upperarm_' + s], RM['lowerarm_' + s], [sx, 0, 0])
        out['lowerarm_' + s] = ang(RM['lowerarm_' + s], RM['hand_' + s], [sx, 0, 0])
        if RM['hand_' + s] >= 0:
            tip = _far_child(nodes, [nd['name'] for nd in nodes], RM['hand_' + s], 'end')
            if tip >= 0:
                out['hand_' + s] = ang(RM['hand_' + s], tip, [sx, 0, 0])
        out['thigh_' + s] = ang(RM['thigh_' + s], RM['calf_' + s], [0, -1, 0])
        out['calf_' + s] = ang(RM['calf_' + s], RM['foot_' + s], [0, -1, 0])
    sp = [RM[k] for k in ('spine_01', 'spine_02', 'spine_03', 'neck_01', 'head') if RM[k] >= 0]
    for i, (a, b) in enumerate(zip(sp, sp[1:])):
        out['spine%d' % i] = ang(a, b, [0, 1, 0])
    if report:
        row = '  '.join('%s %5.1f' % (k, v) for k, v in out.items() if v is not None)
        report('  angles off axis (deg): ' + (row or 'nothing resolved to measure'))
    return out


# ----------------------------------------------------------------- the bake
def bake_tpose(nodes, meshes, clips, qov, report=print, tov=None):
    """Re-rest the rig in the pose qov (+ translation overrides tov): the skin is baked in it, the
    inverse binds rebuilt, the rest replaced. Clips get a hold track for every moved bone they do
    not drive."""
    tov = tov or {}
    if not qov and not tov:
        return
    W0 = RW.world_matrices(nodes)
    posed = [dict(nd, q=list(qov.get(i, nd['q'])), t=list(tov.get(i, nd['t']))) for i, nd in enumerate(nodes)]
    W1 = RW.world_matrices(posed)
    moved = 0.0
    for m in meshes:
        bind = m.get('bind') or RW.m4_identity()
        pal0 = [RW.m4_mul(W0[b], m['ibm'][k * 16:(k + 1) * 16]) for k, b in enumerate(m['boneMap'])]
        pal0 = [RW.m4_mul(P, bind) for P in pal0]
        pal1 = [W1[b] for b in m['boneMap']]
        # old rig-space -> new rig-space per bone: W1[b] * inv(W0[b]) * (old palette incl bind)
        X = [RW.m4_mul(pal1[k], RW.m4_mul(RW.m4_inverse(W0[b]), pal0[k])) for k, b in enumerate(m['boneMap'])]
        pos, nrm = m['pos'], m.get('nrm')
        nv = len(pos) // 3
        newp, newn = [0.0] * (nv * 3), ([0.0] * (nv * 3) if nrm else None)
        for v in range(nv):
            x, y, z = pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]
            ox = oy = oz = 0.0
            nx = ny = nz = 0.0
            for k in range(4):
                w = m['weights'][v][k]
                if w <= 0:
                    continue
                M = X[m['joints'][v][k]]
                ox += w * (M[0] * x + M[4] * y + M[8] * z + M[12])
                oy += w * (M[1] * x + M[5] * y + M[9] * z + M[13])
                oz += w * (M[2] * x + M[6] * y + M[10] * z + M[14])
                if nrm:
                    a, b, c = nrm[v * 3], nrm[v * 3 + 1], nrm[v * 3 + 2]
                    nx += w * (M[0] * a + M[4] * b + M[8] * c)
                    ny += w * (M[1] * a + M[5] * b + M[9] * c)
                    nz += w * (M[2] * a + M[6] * b + M[10] * c)
            newp[v * 3], newp[v * 3 + 1], newp[v * 3 + 2] = ox, oy, oz
            if nrm:
                L = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                newn[v * 3], newn[v * 3 + 1], newn[v * 3 + 2] = nx / L, ny / L, nz / L
            moved = max(moved, abs(ox - x) + abs(oy - y) + abs(oz - z))
        m['pos'] = newp
        if nrm:
            m['nrm'] = newn
        m['bind'] = RW.m4_identity()
        m['ibm'] = [c for b in m['boneMap'] for c in RW.m4_inverse(W1[b])]
    # hold tracks: a clip authored against the old rest keeps playing as authored
    for ci, (nm, fps, frames, tracks) in enumerate(clips):
        added = 0
        for b, q1 in qov.items():
            if (b, 0) not in tracks:
                tracks[(b, 0)] = list(nodes[b]['q']) * frames
                added += 1
        for b, t1 in tov.items():
            if (b, 1) not in tracks:
                tracks[(b, 1)] = list(nodes[b]['t']) * frames
                added += 1
        if added and report:
            report('  clip %s: %d hold track(s) added for bones the T-pose moved' % (nm, added))
    for i, nd in enumerate(nodes):
        if i in qov:
            nd['q'] = list(qov[i])
        if i in tov:
            nd['t'] = list(tov[i])
    if report:
        report('  bake: rest pose is now the T-pose; the farthest vertex moved %.4f in mesh space (expected: the pose change)' % moved)


# ----------------------------------------------------------------- the picture
def cloud_sheet(meshes, nodes, path, size=420, RM=None):
    """Front / side / top of the skinned bind-pose cloud with the skeleton over it: the thing to
    LOOK at before claiming a T-pose. Pillow; no engine."""
    from PIL import Image, ImageDraw
    W = RW.world_matrices(nodes)
    pts = []
    for m in meshes:
        pal = [RW.m4_mul(W[b], m['ibm'][k * 16:(k + 1) * 16]) for k, b in enumerate(m['boneMap'])]
        bind = m.get('bind') or RW.m4_identity()
        pos = m['pos']
        step = max(1, len(pos) // 3 // 6000)
        for v in range(0, len(pos) // 3, step):
            x, y, z = pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]
            bx = bind[0] * x + bind[4] * y + bind[8] * z + bind[12]
            by = bind[1] * x + bind[5] * y + bind[9] * z + bind[13]
            bz = bind[2] * x + bind[6] * y + bind[10] * z + bind[14]
            ox = oy = oz = 0.0
            for k in range(4):
                w = m['weights'][v][k]
                if w <= 0:
                    continue
                M = pal[m['joints'][v][k]]
                ox += w * (M[0] * bx + M[4] * by + M[8] * bz + M[12])
                oy += w * (M[1] * bx + M[5] * by + M[9] * bz + M[13])
                oz += w * (M[2] * bx + M[6] * by + M[10] * bz + M[14])
            pts.append((ox, oy, oz))
    if not pts:
        return
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    span = max(hi[k] - lo[k] for k in range(3)) or 1.0
    cen = [(lo[k] + hi[k]) * 0.5 for k in range(3)]
    im = Image.new('RGB', (size * 3, size), (24, 24, 28))
    dr = ImageDraw.Draw(im)
    views = [(0, 1, 1, 'front (+z toward you)'), (2, 1, -1, 'side (from +x)'), (0, 2, 1, 'top')]

    def proj(p, vi):
        a, b, sgn, _ = views[vi]
        u = (p[a] - cen[a]) / span * sgn * (size * 0.85) + size * 0.5 + vi * size
        v = size * 0.5 - (p[b] - cen[b]) / span * (size * 0.85)
        return u, v
    for vi in range(3):
        for p in pts:
            u, v = proj(p, vi)
            im.putpixel((int(max(0, min(size * 3 - 1, u))), int(max(0, min(size - 1, v)))), (150, 160, 175))
        bones = [(i, nd['parent']) for i, nd in enumerate(nodes) if nd['parent'] >= 0 and nd.get('kind', 1) != 2]
        for i, p in bones:
            a, b = proj(W[i][12:15], vi), proj(W[p][12:15], vi)
            dr.line([a, b], fill=(255, 190, 60), width=2)
        if RM:
            for key, col in (('upperarm_l', (80, 220, 120)), ('upperarm_r', (220, 80, 120)), ('thigh_l', (80, 220, 120)), ('thigh_r', (220, 80, 120))):
                n = RM.get(key, -1)
                if n >= 0:
                    u, v = proj(W[n][12:15], vi)
                    dr.ellipse([u - 4, v - 4, u + 4, v + 4], outline=col, width=2)
        dr.text((vi * size + 8, 6), views[vi][3], fill=(230, 230, 230))
    dr.text((8, size - 16), 'span %.3f  y %.3f..%.3f  (%d points, %d bones)' % (span, lo[1], hi[1], len(pts), len(nodes)), fill=(200, 200, 200))
    im.save(path)
    return path


def parse_map(spec):
    out = {}
    for item in (spec or '').split(','):
        if not item.strip():
            continue
        k, v = item.split('=', 1)
        out[k.strip()] = v.strip()
    return out


# ----------------------------------------------------------------- as a tool, on a finished pack
def _scene_from_pack(path):
    import struct
    from rpack import Rpack
    p = Rpack(path)
    nodes = p.nodes()
    for i, nd in enumerate(nodes):
        nd['name'] = p.node_names()[i]
    meshes = []
    for mi in range(p.nMeshes):
        rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        name, node, nv, ni, nb, flags, oP, oN, oU, oJ, oW, oI, oBM, oIBM, oBind = rec
        pos = list(struct.unpack_from('<%df' % (nv * 3), p.buf, oP))
        nrm = list(struct.unpack_from('<%df' % (nv * 3), p.buf, oN)) if flags & 1 else None
        uv = list(struct.unpack_from('<%df' % (nv * 2), p.buf, oU)) if flags & 2 else None
        jb = p.buf[oJ:oJ + nv * 4]
        wb = p.buf[oW:oW + nv * 4]
        joints = [[jb[v * 4 + k] for k in range(4)] for v in range(nv)]
        weights = [[wb[v * 4 + k] / 255.0 for k in range(4)] for v in range(nv)]
        idx = list(struct.unpack_from('<%dI' % ni, p.buf, oI))
        bm = list(struct.unpack_from('<%dH' % nb, p.buf, oBM))
        ibm = list(struct.unpack_from('<%df' % (16 * nb), p.buf, oIBM))
        bind = list(struct.unpack_from('<16f', p.buf, oBind))
        mat = p.buf[p.offMats + mi * 36: p.offMats + (mi + 1) * 36]
        tex = struct.unpack_from('<i', mat, 24)[0]
        ntex = struct.unpack_from('<i', mat, 28)[0] - 1
        mfl = struct.unpack_from('<I', mat, 16)[0]
        meshes.append(dict(name=p.sname(name), node=node, pos=pos, nrm=nrm, uv=uv, idx=idx, joints=joints, weights=weights,
                           boneMap=bm, ibm=ibm, bind=bind, tex=tex, ntex=ntex, blend=bool(flags & 8),
                           nearest=bool(mfl & 1), flipv=bool(mfl & 2)))
    clips = []
    for ci in range(p.nClips):
        c = p.clip(ci)
        tr = {}
        for (node, typ, data) in p.tracks(ci):
            comp = 4 if typ == 0 else 3
            tr[(node, typ)] = list(struct.unpack('<%df' % (c['frames'] * comp), data))
        clips.append((c['name'], c['fps'], c['frames'], tr))
    texTab = p.offTexTable
    textures = []
    for t in range(p.nTex):
        off, ln = struct.unpack_from('<II', p.buf, texTab + t * 16 + 8)
        textures.append(p.buf[off:off + ln])
    return nodes, meshes, clips, textures


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, dst = argv[1], argv[2]
    nodes, meshes, clips, textures = _scene_from_pack(src)
    names = [nd['name'] for nd in nodes]
    if '--bones' in argv:
        print_bones(nodes, names)
    ov = parse_map(argv[argv.index('--map') + 1]) if '--map' in argv else {}
    RM = canon_map(nodes, names, ov)
    print('  before:')
    measure(nodes, RM)
    qov = solve_tpose(nodes, RM)
    print('  after:')
    measure(nodes, RM, qov)
    tov = symmetrise(nodes, RM) if '--symmetric' in argv else None
    tov = ground_lift(nodes, RM, qov, tov, meshes=meshes)
    bake_tpose(nodes, meshes, clips, qov, tov=tov)
    # statue2body's two-frame 'tpose' stub (there so a baker finds a clip) would replay the OLD pose
    # through its hold tracks: it becomes a two-frame hold of the NEW rest, so the lab shows the T-pose
    if any(c[0] == 'tpose' for c in clips):
        clips = [c for c in clips if c[0] != 'tpose']
        tr = {(i, 0): list(nd['q']) * 2 for i, nd in enumerate(nodes) if nd.get('kind') == 1}
        if RM['pelvis'] >= 0:
            tr[(RM['pelvis'], 1)] = list(nodes[RM['pelvis']]['t']) * 2
        clips.append(('tpose', 30.0, 2, tr))
        print("  clip tpose: rewritten as a two-frame hold of the new rest")
    RW.write_pack(dst, nodes, meshes, clips, textures)
    RW.bounds_report(nodes, meshes)
    sheet = os.path.splitext(dst)[0] + '_tpose.png'
    cloud_sheet(meshes, nodes, sheet, RM=RM)
    print('  sheet %s -- LOOK at it' % sheet)
    if '--proof' in argv:
        import proofrender
        proofrender.render(nodes, meshes, textures, argv[argv.index('--proof') + 1], bones=True, RM=RM)
        print('  proof %s -- front and side, lit, orthographic' % argv[argv.index('--proof') + 1])
    if '--dae' in argv:
        import daelib
        daelib.write_dae(argv[argv.index('--dae') + 1], nodes, meshes, clips, textures, names)
    if '--glb' in argv:
        import glbwrite
        glbwrite.write_glb(argv[argv.index('--glb') + 1], nodes, meshes, clips, textures, names)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
