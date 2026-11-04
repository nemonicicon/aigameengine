#!/usr/bin/env python3
"""soma.py -- a Kimodo take as a body the kit can read: the SOMA-30 skeleton and one generated motion behind the Rpack interface, so rig_map,
fk_world, clip_pose, retarget.Basis and the benches work on it without a pack on disk. The first half of the text-to-motion lane (11-02).

    take = SomaTake("C:/MyVeryOwnVideoGames/kimodo/out/walk_s1")     # a folder kmd-generate wrote: root_positions.f32, local_rotations_xyzw.f32

The names are NOT passed through. ⛔ SOMA calls the THIGH "LeftLeg" and the shin "LeftShin"; the kit's matchers read LeftLeg as the shin (Mixamo's
word for it) and would lay thigh rotation on the shin. Its three spine bones and two neck bones are renamed to what the canon resolves too.
Rest: a T-pose standing on nothing in particular, metres, Y up, facing +Z, identity local rotations, so a local rotation is the pose outright.
Skeleton data is NVIDIA Kimodo's (Apache-2.0), as kimodo.cpp's src/skeleton.hpp carries it. No motion capture is in this file or in this lane: a take is
what the model generated.
"""
import os, struct

NAMES = ["Hips", "Spine1", "Spine2", "Chest", "Neck1", "Neck2", "Head", "Jaw", "LeftEye", "RightEye",
         "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "LeftHandThumbEnd", "LeftHandMiddleEnd",
         "RightShoulder", "RightArm", "RightForeArm", "RightHand", "RightHandThumbEnd", "RightHandMiddleEnd",
         "LeftLeg", "LeftShin", "LeftFoot", "LeftToeBase", "RightLeg", "RightShin", "RightFoot", "RightToeBase"]
PARENTS = [-1, 0, 1, 2, 3, 4, 5, 6, 6, 6, 3, 10, 11, 12, 13, 13, 3, 16, 17, 18, 19, 19, 0, 22, 23, 24, 0, 26, 27, 28]
OFFSETS = [[0.0, 0.0, 0.0], [-0.00013727, 0.0500376256, -0.00053726669], [-1.86574103e-09, 0.0712530139, -0.000298248546],
           [-5.75188398e-09, 0.0755006305, -0.00815970992], [-0.00181676517, 0.263112953, -0.00553348292],
           [-2.85102231e-08, 0.0770939664, 0.0230258546], [-4.5975437e-08, 0.0612891595, 0.0195370861],
           [2.63687901e-05, 0.0047559225, 0.0309494062], [0.0320638079, 0.0538020513, 0.0758688308],
           [-0.0322244017, 0.05361869, 0.0755823359], [0.0162165175, 0.232371641, 0.0511341324],
           [0.149198457, 2.19397873e-08, -0.0550232576], [0.287393078, 2.50268389e-09, -2.58787737e-05],
           [0.270939812, -7.06625108e-09, 2.60897248e-05], [0.122686267, -0.0322017573, 0.0483306876],
           [0.190119595, -0.00312878387, -0.000339570373], [-0.0138011824, 0.231803086, 0.0521415786],
           [-0.150371962, 1.17387901e-07, -0.0554560437], [-0.287366393, 1.87628082e-08, -2.59709359e-05],
           [-0.271336198, -1.16767401e-09, 2.61269368e-05], [-0.122642483, -0.0321145448, 0.0480403904],
           [-0.190005945, -0.00306615542, -0.0003157343], [0.10043214, -0.0843452671, 0.0259565473],
           [-1e-08, -0.432217537, -0.00802912805], [1e-08, -0.421550959, -0.0348152298],
           [0.0, -0.0505947206, 0.132315294], [-0.10047278, -0.0829525995, 0.0262031695],
           [1e-08, -0.433622059, -0.00805555828], [2e-08, -0.421173943, -0.0347839785],
           [-3.42907669e-09, -0.0507960932, 0.132841956]]
# What the canon calls them. Neck2 and the markers fall outside it: their parts of the pose ride along in the world rotations of what's above and below.
RENAME = {"Spine1": "Spine", "Spine2": "Spine1", "Chest": "Spine2", "Neck1": "Neck", "Neck2": "UpperNeck",
          "LeftLeg": "LeftUpLeg", "LeftShin": "LeftLeg", "RightLeg": "RightUpLeg", "RightShin": "RightLeg"}
FPS = 30.0


class SomaTake:
    """Reads like an Rpack with one clip, "take"."""
    def __init__(self, folder):
        self.folder = folder
        with open(os.path.join(folder, "root_positions.f32"), "rb") as f:
            root = f.read()
        with open(os.path.join(folder, "local_rotations_xyzw.f32"), "rb") as f:
            rots = f.read()
        self.frames = len(root) // 12
        assert len(rots) == self.frames * len(NAMES) * 16, "%s: %d bytes of rotation for %d frames" % (folder, len(rots), self.frames)
        self._root, self._rots = root, rots

    def clip_names(self):
        return ["take"]

    def clip(self, ci):
        return {"name": "take", "fps": FPS, "frames": self.frames}

    def nodes(self):
        return [dict(name=self.node_names()[i], parent=PARENTS[i], kind=1, t=list(OFFSETS[i]), q=[0.0, 0.0, 0.0, 1.0], s=[1.0, 1.0, 1.0]) for i in range(len(NAMES))]

    def node_names(self):
        return [RENAME.get(n, n) for n in NAMES]

    def node_index(self, name):
        return self.node_names().index(name)

    def tracks(self, ci):
        out = []
        for j in range(len(NAMES)):
            data = b"".join(self._rots[(f * len(NAMES) + j) * 16:(f * len(NAMES) + j + 1) * 16] for f in range(self.frames))
            out.append((j, 0, data))
        out.append((0, 1, self._root))
        return out

    def root(self, f):
        return list(struct.unpack_from("<3f", self._root, f * 12))

    def local(self, f):
        """Every joint's local rotation on this frame, {node: q}."""
        return {j: list(struct.unpack_from("<4f", self._rots, (f * len(NAMES) + j) * 16)) for j in range(len(NAMES))}

    def pose(self, f):
        """({node: q}, {node: t}) for fk_world, on a whole frame."""
        return self.local(f), {0: self.root(f)}
