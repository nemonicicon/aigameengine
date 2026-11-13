import subprocess, sys, os, shutil
import numpy as np
from PIL import Image, ImageDraw
FF = os.environ.get("FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"
src, out = sys.argv[1], sys.argv[2]
idx = [int(x) for x in sys.argv[3].split(",")]
W, H = 320, 240
p = subprocess.run([FF, "-hide_banner", "-loglevel", "error", "-i", src, "-vf", f"scale={W}:{H}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True)
a = np.frombuffer(p.stdout, dtype=np.uint8)
n = a.size // (W * H * 3)
a = a[:n * W * H * 3].reshape(n, H, W, 3)
cols = 4
rows = (len(idx) + cols - 1) // cols
sheet = Image.new("RGB", (W * cols, H * rows))
dr = ImageDraw.Draw(sheet)
for k, i in enumerate(idx):
    x, y = (k % cols) * W, (k // cols) * H
    sheet.paste(Image.fromarray(a[i]), (x, y))
    dr.rectangle((x, y, x + 70, y + 12), fill=(0, 0, 0))
    dr.text((x + 3, y + 1), f"frame {i}", fill=(255, 255, 0))
sheet.save(out)
