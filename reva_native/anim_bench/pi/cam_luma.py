"""Read the webcam recording of tube_lab: how the TV behaves on a mode switch, and whether the field-locked stripes changed."""
import subprocess, sys, os, shutil
import numpy as np
FF = os.environ.get("FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"
src = sys.argv[1]
W, H = 160, 120
p = subprocess.run([FF, "-hide_banner", "-loglevel", "error", "-i", src, "-vf", f"scale={W}:{H}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True)
a = np.frombuffer(p.stdout, dtype=np.uint8)
n = a.size // (W * H * 3)
a = a[:n * W * H * 3].reshape(n, H, W, 3)
# inside the picture: the camera sees the tube roughly x 80..570, y 35..405 of 640x480
roi = a[:, 20:95, 30:130, :].astype(np.float32)
rgb = roi.mean(axis=(1, 2))
y = rgb @ np.array([0.299, 0.587, 0.114])
top = a[:, 15:40, 30:130, :].astype(np.float32).mean(axis=(1, 2, 3))
bot = a[:, 75:100, 30:130, :].astype(np.float32).mean(axis=(1, 2, 3))
fps = 30.0
print(f"{n} frames, {n/fps:.1f} s")
state = []
for i in range(n):
    r, g, b = rgb[i]
    if y[i] < 18: s = "black"
    elif b > g * 1.25: s = "blue"
    elif g > b * 1.25: s = "green"
    else: s = "other"
    state.append(s)
# runs of the same state
runs = []
start = 0
for i in range(1, n + 1):
    if i == n or state[i] != state[start]:
        runs.append((state[start], start, i - start))
        start = i
print("state runs (state, start s, frames):")
for s, st, ln in runs:
    print(f"  {s:6s} {st/fps:6.2f} s  {ln:4d} frames  Y {y[st:st+ln].mean():6.1f}  rgb {rgb[st:st+ln].mean(axis=0).round(1)}")
# one-second luminance timeline
print("Y per second:")
for t in range(int(n / fps)):
    seg = y[int(t * fps):int((t + 1) * fps)]
    print(f"  {t:3d} s  mean {seg.mean():6.1f}  min {seg.min():6.1f}  max {seg.max():6.1f}  std {seg.std():5.2f}")
