"""hash_run.py -- one fixed-step run of the zombie game with no input, printing its [simhash] lines. For comparing two builds or two machines.

usage: hash_run.py <exe> <out.txt> [--packs DIR] [--hero PATH] [--ticks N] [--env K=V ...]   (Windows; the Pi's side is the same env through pi_run.sh)"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time

ap = argparse.ArgumentParser()
ap.add_argument("exe"); ap.add_argument("out")
ap.add_argument("--packs", default="packs"); ap.add_argument("--hero", default="packs/v1_Character_17_Female_Police.rpack")
ap.add_argument("--ticks", type=int, default=1800); ap.add_argument("--env", action="append", default=[]); ap.add_argument("--slots", default="100")
a = ap.parse_args()
NATIVE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # machine-independent paths
env = dict(os.environ, PATH=r"C:\msys64\mingw64\bin;" + os.environ["PATH"], REVA_WINDOWED="1", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60",
           REVA_AUTOMODE="9", REVA_AUTOVAR="1", REVA_AUTOPICK="0", REVA_TIER="pi")
for e in a.env:
    k, v = e.split("=", 1); env[k] = v
log = []
p = subprocess.Popen([a.exe, a.slots, a.packs, a.hero], cwd=NATIVE, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", bufsize=1)
threading.Thread(target=lambda: [log.append(l) for l in p.stdout], daemon=True).start()
t0 = time.time()
last = 0
while time.time() - t0 < 300 and p.poll() is None:
    time.sleep(0.5)
    for l in log[-20:]:
        m = re.match(r"\[simhash\] tick (\d+)", l)
        if m:
            last = max(last, int(m.group(1)))
    if last >= a.ticks:
        break
p.kill(); p.wait(timeout=15); time.sleep(0.3)
lines = [l for l in log if l.startswith("[simhash]")]
open(a.out, "w").writelines(lines)
packs = [l.strip() for l in log if "packs loaded" in l][:1]
print(f"{os.path.basename(a.exe)}: {len(lines)} hash lines, last tick {last}, took {time.time()-t0:.0f} s, {packs}")
