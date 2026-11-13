"""scene_here.py -- driver.py's own `scene` command on LINUX (a cloud machine, no GPU), with nothing in driver.py edited (11-11).
It loads the driver as a module and replaces only the three things that are Windows-only on the scene's road:
  kill()  taskkill -> pkill of the Linux engine
  win()   the backslashed Windows path -> the POSIX path as it is
  NATIVE  the tree the engine runs in (REVA_NATIVE): a checkout has no packs/, and none may be made in it, so a take's packs, props, sets and
          scenes sit in a stage directory OUTSIDE the repository, and that's the engine's working directory
The Vulkan build needs a window, so run it under xvfb-run; pixels with no GPU come from Mesa's lavapipe (apt: libsdl2-dev libvulkan-dev libstb-dev
mesa-vulkan-drivers xvfb, then `GAME=games/<game>/<game>.c bash build_linux.sh /abs/crowd_linux`). crowd_soft with --env REVA_NOWINDOW=1 needs none.
    REVA_NATIVE=/abs/stage xvfb-run -a -s "-screen 0 1280x720x24" python3 scene_here.py scene OUT.mp4 --name <scene> --set <set> --subject <hero> \
        --partner off --seconds 6 --fps 30 --res 1280x720 --stall 60 --exe /abs/crowd_linux --env REVA_NOSAVE=1 --env REVA_GPUDUTY=1 [--film]
Measured on four cores with the sample body (11-11): about 20 pictures a second at 1280 x 720 and 10 to 16 at 1920 x 1080.
"""
import importlib.util, os, subprocess, sys
DRV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "driver.py")
spec = importlib.util.spec_from_file_location("driver", DRV)
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)
if os.environ.get("REVA_NATIVE"):
    d.NATIVE = os.path.abspath(os.environ["REVA_NATIVE"])
SKIP = os.environ.get("SKIP", "").split(",")   # SKIP=kill,win leaves that part as the driver has it, to show what it does here
if "win" not in SKIP:
    d.win = lambda p: os.path.abspath(p)


def kill(_a=None):
    subprocess.run(["pkill", "-f", r"/crowd_(linux|soft)[^ /]* 400 packs "], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return 0


if "kill" not in SKIP:
    d.kill = kill
sys.exit(d.main())
