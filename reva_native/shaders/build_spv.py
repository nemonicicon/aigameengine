"""build_spv.py -- compiles the renderer's GLSL to SPIR-V and writes ../gfx_spv.h. SPIR-V is the same on every machine, so the header is committed and a build needs no shader compiler; run this
only when a shader changes. Needs glslangValidator (MSYS2's mingw-w64-x86_64-glslang, or Debian's glslang-tools)."""
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
tool = shutil.which("glslangValidator") or r"C:\msys64\mingw64\bin\glslangValidator.exe"
env = dict(os.environ)
if os.name == "nt":
    env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
# a source built a second way, with something switched on: (the file, its name in the header, the switch). What it builds the plain way doesn't change by a byte
ALSO = [("mesh.frag", "mesh_mouth_frag", "-DMOUTH_HOLE=1"), ("mesh_skin.vert", "mesh_skin_mouth_vert", "-DMOUTH_HOLE=1"),
        ("mesh.frag", "mesh_toon_frag", "-DTOON=1"), ("mesh.frag", "mesh_toon_mouth_frag", "-DTOON=1 -DMOUTH_HOLE=1"), ("mesh.frag", "mesh_toon_blend_frag", "-DTOON=1 -DTOON_BLEND=1"),
        ("mesh_skin.vert", "mesh_skin_ink_vert", "-DINK=1"), ("mesh_static.vert", "mesh_static_ink_vert", "-DINK=1"),
        ("mesh_skin.vert", "mesh_skin_fur_vert", "-DFUR=1"), ("mesh_static.vert", "mesh_static_fur_vert", "-DFUR=1"), ("mesh.frag", "mesh_fur_frag", "-DFUR=1"), ("mesh.frag", "mesh_toon_fur_frag", "-DTOON=1 -DFUR=1"),
        ("mesh.frag", "mesh_refl_frag", "-DREFL=1"), ("mesh.frag", "mesh_toon_refl_frag", "-DTOON=1 -DREFL=1"), ("mesh.frag", "mesh_gloss_frag", "-DGLOSS=1"), ("mesh.frag", "mesh_toon_gloss_frag", "-DTOON=1 -DGLOSS=1")]
out = ["/* gfx_spv.h -- the renderer's shaders as SPIR-V. Written by shaders/build_spv.py; edit the GLSL beside it, not this. */"]
n = 0
with tempfile.TemporaryDirectory() as tmp:
    jobs = [(src, os.path.basename(src).replace(".", "_"), None) for src in sorted(glob.glob(os.path.join(HERE, "*.vert")) + glob.glob(os.path.join(HERE, "*.frag")))]
    jobs += [(os.path.join(HERE, a), b, c) for a, b, c in ALSO]
    for src, name, switch in jobs:
        spv = os.path.join(tmp, name + ".spv")
        r = subprocess.run([tool, "-V"] + (switch.split() if switch else []) + ["-o", spv, src], cwd=HERE, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode:
            sys.exit(r.stdout)
        b = open(spv, "rb").read()
        words = struct.unpack("<%dI" % (len(b) // 4), b)
        out.append("__attribute__((unused)) static const uint32_t spv_%s[] = {" % name)   # a shader that's written before its pipeline is doesn't warn
        for i in range(0, len(words), 12):
            out.append("  " + ",".join("0x%08x" % w for w in words[i:i + 12]) + ",")
        out.append("};")
        n += 1
with open(os.path.join(HERE, "..", "gfx_spv.h"), "w", newline="\n") as f:
    f.write("\n".join(out) + "\n")
print(n, "shaders ->", "gfx_spv.h")
