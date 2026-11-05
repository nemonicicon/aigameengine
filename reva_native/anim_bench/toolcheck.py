#!/usr/bin/env python3
"""toolcheck.py -- THE PINNED TOOLCHAIN (queue step 28). What the engine was last built and blessed with, by exact version, and whether this machine still has it.

    toolcheck.py                 this machine against reva_native/toolchain.lock: what's the same, what's moved
    toolcheck.py --write         pin this machine as it stands today (its own section of the lock; the others are left alone)
    toolcheck.py --write --container revalinux     also pin the build container: its Debian packages, read from inside it, and its image
    toolcheck.py --copy DIR      Windows: the pinned packages' own files, out of the package manager's cache into DIR. A dated copy, to build from if a version is ever gone
    toolcheck.py --print         this machine's section as it would be written (what --container asks the container for)

The receipts are the proof that a build is right. This is the other half: when a receipt parts and nothing in the tree changed, the first question is whether the
compiler did, and this answers it in a second. A difference isn't a failure, so this exits 0 unless the lock has no section for this machine; it's a reason to run the
receipts again and, once they hold, to pin again. Every build line already says -std=gnu17 -ffp-contract=off, which is the part of the pin that's in the code.
"""
import datetime
import os
import platform
import re
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOCK = os.path.join(ROOT, "toolchain.lock")
MSYS = r"C:\msys64"
# what a build of the engine and its checks is made with. Windows: MSYS2's mingw64 packages. Debian (the build container, a Pi): its own
WIN_PKGS = ["gcc", "gcc-libs", "binutils", "crt", "headers", "winpthreads", "libwinpthread", "SDL2", "stb", "vulkan-headers", "vulkan-loader", "glslang", "gmp", "mpfr", "mpc", "isl", "zlib", "zstd", "windows-default-manifest"]
DEB_PKGS = ["gcc", "gcc-14", "libc6-dev", "binutils", "gcc-aarch64-linux-gnu", "gcc-14-aarch64-linux-gnu", "libsdl2-dev", "libsdl2-dev:arm64", "libvulkan-dev", "libvulkan-dev:arm64", "libdrm-dev", "libdrm-dev:arm64",
            "libstb-dev", "mesa-vulkan-drivers", "qemu-user", "python3", "python3-pil", "python3-numpy"]


def run(cmd):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=120).stdout
    except Exception as e:
        return "?? %s" % e


def key():
    if os.name == "nt":
        return "windows msys2 mingw64"
    rel = {}
    try:
        for ln in open("/etc/os-release"):
            k, _, v = ln.strip().partition("=")
            rel[k] = v.strip('"')
    except OSError:
        pass
    return "linux %s %s %s" % (rel.get("ID", "?"), rel.get("VERSION_ID", "?"), platform.machine())


def here():
    """this machine's lines: a name, then its version"""
    out = []
    if os.name == "nt":
        q = run([os.path.join(MSYS, "usr", "bin", "pacman.exe"), "-Q"] + ["mingw-w64-x86_64-" + p for p in WIN_PKGS])
        for ln in q.splitlines():
            m = re.match(r"mingw-w64-x86_64-(\S+) (\S+)", ln)
            if m:
                out.append((m.group(1), m.group(2)))
        g = run([os.path.join(MSYS, "mingw64", "bin", "gcc.exe"), "--version"]).splitlines()
        out.append(("gcc --version", g[0].strip() if g else "?"))
        icd = os.environ.get("REVA_LAVAPIPE") or os.path.normpath(os.path.join(ROOT, "..", "..", "tools", "mesa-lavapipe", "x64", "lvp_icd.x86_64.json"))
        if os.path.exists(icd):
            dll = os.path.join(os.path.dirname(icd), "vulkan_lvp.dll")
            out.append(("lavapipe", "%d bytes, %s" % (os.path.getsize(dll), datetime.date.fromtimestamp(os.path.getmtime(dll)).isoformat()) if os.path.exists(dll) else "its ICD is there and its DLL isn't"))
    else:
        for p in DEB_PKGS:
            v = (run(["dpkg-query", "-W", "-f=${Version} ", p]).split() or [""])[0]   # a package on two architectures answers once for each: the first is this one
            if v and "no packages found" not in v and not v.startswith("??") and "dpkg-query" not in v:
                out.append((p, v))
        g = run([os.environ.get("CC", "gcc"), "--version"]).splitlines()
        out.append(("gcc --version", g[0].strip() if g else "?"))
    out.append(("python", platform.python_version()))
    for mod in ("PIL", "numpy"):
        try:
            out.append((mod, __import__(mod).__version__))
        except Exception:
            pass
    return out


def read_lock():
    secs, cur = {}, None
    if os.path.exists(LOCK):
        for ln in open(LOCK, encoding="utf-8"):
            ln = ln.rstrip("\n")
            m = re.match(r"^\[(.+?)\](?: +(.*))?$", ln)
            if m:
                cur = m.group(1)
                secs[cur] = {"note": m.group(2) or "", "lines": []}
            elif cur and ln.strip() and not ln.startswith("#"):
                name, _, ver = ln.partition("\t")
                secs[cur]["lines"].append((name.strip(), ver.strip()))
    return secs


def write_lock(secs):
    with open(LOCK, "w", encoding="utf-8", newline="\n") as f:
        f.write("# toolchain.lock -- what the engine was last built and blessed with, by exact version. anim_bench/toolcheck.py writes it and reads it:\n"
                "# `toolcheck.py` says what's moved on this machine, `--write` pins it again once the receipts hold. A section a machine, a name and a tab and its version a line.\n"
                "# Every build line also says -std=gnu17 -ffp-contract=off, and the receipts (driver.py check) are what a pin is for: the same hashes, from the same compiler.\n")
        for k in sorted(secs):
            f.write("\n[%s]%s\n" % (k, (" " + secs[k]["note"]) if secs[k]["note"] else ""))
            for name, ver in secs[k]["lines"]:
                f.write("%s\t%s\n" % (name, ver))


def main(argv):
    if "--print" in argv:
        print("KEY " + key())
        for n, v in here():
            print("%s\t%s" % (n, v))
        return 0
    secs = read_lock()
    k = key()
    mine = here()
    today = datetime.date.today().isoformat()
    if "--write" in argv:
        secs[k] = {"note": "pinned " + today, "lines": mine}
        if "--container" in argv:
            box = argv[argv.index("--container") + 1]
            o = run(["docker", "exec", box, "python3", "/src/anim_bench/toolcheck.py", "--print"])
            ck, lines = None, []
            for ln in o.splitlines():
                if ln.startswith("KEY "):
                    ck = ln[4:].strip()
                elif "\t" in ln:
                    a, _, b = ln.partition("\t")
                    lines.append((a, b))
            if not ck:
                print("the container %s didn't answer: %s" % (box, o[-300:]))
                return 1
            img = run(["docker", "inspect", box, "--format", "{{.Config.Image}} {{.Image}}"]).strip()
            secs[ck] = {"note": "pinned %s, the build container %s" % (today, box), "lines": [("image", img)] + lines}   # under the machine's own name, so the check run inside it finds it
        write_lock(secs)
        print("pinned: %s" % ", ".join(sorted(secs)))
        return 0
    if "--copy" in argv:
        if os.name != "nt":
            print("--copy is for Windows: on Debian the pin is the package versions, and snapshot.debian.org keeps every one of them")
            return 1
        dst = argv[argv.index("--copy") + 1]
        cache = os.path.join(MSYS, "var", "cache", "pacman", "pkg")
        os.makedirs(dst, exist_ok=True)
        got, missing, size = 0, [], 0
        for n, v in (secs.get(k) or {"lines": mine})["lines"]:
            if n not in WIN_PKGS:
                continue
            f = "mingw-w64-x86_64-%s-%s-any.pkg.tar.zst" % (n, v)
            for ext in ("", ".sig"):
                p = os.path.join(cache, f + ext)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(dst, f + ext))
                    size += os.path.getsize(p)
                    got += ext == ""
                elif ext == "":
                    missing.append(f)
        shutil.copy2(LOCK, os.path.join(dst, "toolchain.lock")) if os.path.exists(LOCK) else None
        with open(os.path.join(dst, "README.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("The engine's Windows toolchain as pinned %s (reva_native/toolchain.lock, beside this): MSYS2's own package files, with their signatures.\n"
                    "To build with exactly these again, in an MSYS2 shell:    pacman -U *.pkg.tar.zst\n"
                    "They are MSYS2's packages, unchanged, each under its own licence; this folder is a copy for our own use and isn't distributed.\n" % today)
        print("%d packages, %.0f MB, in %s%s" % (got, size / 1048576.0, dst, ("; not in the cache any more: " + ", ".join(missing)) if missing else ""))
        return 0
    if k not in secs:
        print("toolchain.lock has no section for this machine (%s): `toolcheck.py --write` pins it" % k)
        return 1
    want = dict(secs[k]["lines"])
    want.pop("image", None)   # the container's image is read from outside it
    moved = [(n, want.get(n), v) for n, v in mine if want.get(n) != v]
    gone = [n for n in want if n not in dict(mine)]
    for n, w, v in moved:
        print("  %-24s %s  (pinned: %s)" % (n, v, w or "not pinned"))
    for n in gone:
        print("  %-24s not here  (pinned: %s)" % (n, want[n]))
    print("%s: %s" % (k, "the same as %s" % secs[k]["note"] if not moved and not gone else "%d moved since it was %s. Run the receipts; pin again once they hold" % (len(moved) + len(gone), secs[k]["note"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
