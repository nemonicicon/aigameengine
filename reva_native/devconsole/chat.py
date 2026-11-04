#!/usr/bin/env python3
"""chat.py -- the chatbox: talk to your AI on the one TV, and play what it made. A plain program on the Pi's text console, so it costs next to nothing, needs no GPU, and is still there when the game crashes.

usage: chat.py [options] <your AI's command> [args]     {} in the args is your message. With no {}, the message goes to the command's stdin
       e.g. chat.py claude -p --continue {}
       chat.py --selftest
Any tool that can answer one message and exit will do, and the conversation is its own to remember. On a Pi it runs inside agent.sh's cap, so play.sh can freeze it.

options: --plain      line by line, for anywhere that isn't a real terminal
         --nocap      don't go through agent.sh
         --play "c"   what /play runs, in place of devconsole/play.sh
         --cols N --rows N --left N --top N --dark S    the text box, and how long until it goes dark. On a TV all of this is worked out

On a TV: 35 columns and 12 rows of the console's biggest font, inside the safe box and moved by the player's screen fit (save/display.txt), because that's what a composite picture can show (reports/, section 11). Left alone for five minutes it goes dark, and any key brings it back, the same as the pause menu: static text is what burns a tube.
"""
import glob
import os
import queue
import re
import shutil
import signal
import subprocess
import sys
import textwrap
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.expanduser("~/.aigame_chat.log")
FONTS = ("/usr/share/consolefonts/Uni3-TerminusBold32x16.psf.gz", "/usr/share/consolefonts/Lat15-TerminusBold32x16.psf.gz")
ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[@-Z\\^_]")
ASCII = str.maketrans({"—": "-", "–": "-", "’": "'", "‘": "'", "“": '"', "”": '"', "…": "...", "→": "->", "•": "*", "✓": "ok", "⛔": "!!", "⭐": "*"})
KEYS = {"\x1b[A": "up", "\x1b[B": "down", "\x1b[5~": "pgup", "\x1b[6~": "pgdn", "\x1b[F": "end", "\x1b[4~": "end", "\x1bOF": "end"}
KEEP = 6000   # rows of the conversation it holds; the log has all of it
HELP = ["Type to talk to your AI. Enter sends.", "/play <game folder> <bodies> <hero pack>: play it. /play again plays the same one. Quit from the game's start menu.", "/stop stops the AI's answer. /quit leaves.", "PgUp and PgDn read back, End comes back."]


def clean(raw):
    """what a tool printed, as plain text: no colours, no cursor moves, and only the last state of a progress bar"""
    s = ANSI.sub("", raw.decode("utf-8", "replace")).rstrip("\r\n")
    return "".join(c for c in s.rsplit("\r", 1)[-1] if c >= " " or c == "\t")


def wrap(text, cols):
    if not text.strip():
        return [""]
    return textwrap.wrap(text.expandtabs(4), cols, replace_whitespace=False, break_on_hyphens=False) or [""]


def facts():
    """what's true of the machine right now, from files that cost nothing to read. On a machine without them, nothing"""
    out = []
    try:
        for ln in open("/proc/meminfo"):
            if ln.startswith("MemAvailable"):
                out.append("%.1f GB" % (int(ln.split()[1]) / 1048576))
                break
    except OSError:
        pass
    try:
        out.append("%d C" % (int(open("/sys/class/thermal/thermal_zone0/temp").read()) // 1000))
    except (OSError, ValueError):
        pass
    return " ".join(out)


class Chat:
    """the conversation, the AI's process and the commands. A front end shows it"""

    def __init__(self, cmd, cols, rows, play, cap):
        self.cmd, self.cols, self.rows, self.playcmd, self.cap = cmd, cols, rows, play, cap
        self.lines, self.waiting, self.playargs = [], [], []
        self.back, self.t0, self.on, self.proc = 0, 0.0, True, None
        self.q = queue.Queue()
        self.changed = True
        self.heard = None          # a front end that wants each row as it comes
        self.hand_over = lambda: None   # the terminal goes to a game, and comes back
        self.take_back = lambda: None
        try:
            self.log = open(LOG, "a", encoding="utf-8", errors="replace")
        except OSError:
            self.log = None

    def say(self, text, kind=0):
        """a line of the conversation. kind: 0 the AI, 1 you, 2 this program"""
        if self.log:
            self.log.write(text + "\n")
            self.log.flush()
        for row in wrap(text, self.cols):
            self.lines.append((row, kind))
            if self.back:
                self.back += 1   # reading back stays on what it was reading
            if self.heard:
                self.heard(row, kind)
        if len(self.lines) > KEEP + 1000:
            del self.lines[:1000]
        self.changed = True

    def start(self, msg):
        arg = any("{}" in a for a in self.cmd)
        argv = [a.replace("{}", msg) for a in self.cmd]
        if self.cap:
            argv = ["bash", os.path.join(HERE, "agent.sh")] + argv
        env = dict(os.environ, TERM="dumb", NO_COLOR="1", COLUMNS=str(self.cols))
        try:
            self.proc = subprocess.Popen(argv, stdin=subprocess.DEVNULL if arg else subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, **({"start_new_session": True} if os.name == "posix" else {}))
        except OSError as e:
            self.say("Couldn't start %s: %s" % (self.cmd[0], e), 2)
            return
        if not arg:
            try:
                self.proc.stdin.write((msg + "\n").encode("utf-8"))
                self.proc.stdin.close()
            except OSError:
                pass
        self.t0 = time.time()
        threading.Thread(target=self.reader, args=(self.proc,), daemon=True).start()

    def reader(self, proc):
        for raw in proc.stdout:
            self.q.put(raw)
        self.q.put(None)

    def pump(self):
        """what the AI has said since last time, and the next message once it's done"""
        while True:
            try:
                raw = self.q.get_nowait()
            except queue.Empty:
                return
            if raw is None:
                code = self.proc.wait()
                self.proc = None
                self.changed = True
                if code:
                    self.say("(it stopped, exit %d)" % code, 2)
                if self.waiting:
                    self.start(self.waiting.pop(0))
            else:
                self.say(clean(raw))

    def stop(self):
        if not self.proc:
            self.say("Nothing's running.", 2)
            return
        self.waiting.clear()
        try:
            os.killpg(self.proc.pid, signal.SIGTERM)
        except (AttributeError, OSError):
            self.proc.terminate()
        self.say("(stopped)", 2)

    def play(self, args):
        self.playargs = args or self.playargs
        if not self.playargs:
            self.say("/play <game folder> <bodies> <hero pack>", 2)
            return
        self.say("Playing " + " ".join(self.playargs), 2)
        self.hand_over()
        try:
            out = subprocess.run(self.playcmd + self.playargs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
        except OSError as e:
            out = ("Couldn't start the game: %s\n" % e).encode("utf-8")
        except KeyboardInterrupt:
            out = b"(stopped)\n"
        finally:
            self.take_back()
        for raw in out.splitlines():
            self.say(clean(raw), 2)

    def line(self, text):
        text = text.strip()
        if not text:
            return
        if text.startswith("/"):
            w = text.split()
            if w[0] == "/quit":
                self.on = False
            elif w[0] == "/help":
                for h in HELP:
                    self.say(h, 2)
            elif w[0] == "/stop":
                self.stop()
            elif w[0] == "/play":
                self.play(w[1:])
            else:
                self.say("There's no %s. /help says what there is." % w[0], 2)
            return
        self.say("> " + text, 1)
        self.back = 0
        if self.proc:
            self.waiting.append(text)
            self.say("(after this answer)", 2)
        else:
            self.start(text)

    def status(self, now):
        if self.proc:
            s = int(now - self.t0)
            left = "WORKING %d:%02d" % (s // 60, s % 60)
        else:
            left = "READY"
        if self.back:
            left = "BACK %d, END RETURNS" % self.back
        right = facts()
        return left + " " * max(1, self.cols - len(left) - len(right)) + right if len(left) + len(right) < self.cols else left[:self.cols]

    def frame(self, typed, now):
        """the whole box as rows of (text, kind): the facts on top, the conversation, and what you're typing, of which three rows at most"""
        s = "> " + typed
        inp = [s[i:i + self.cols] for i in range(0, len(s), self.cols)]
        if len(s) % self.cols == 0:
            inp.append("")
        inp = inp[-3:]
        h = self.rows - 1 - len(inp)
        self.back = max(0, min(self.back, len(self.lines) - h))
        end = len(self.lines) - self.back
        body = self.lines[max(0, end - h):end]
        return [(self.status(now), 3)] + body + [("", 0)] * (h - len(body)) + [(r, 1) for r in inp], len(inp[-1])


def plain(chat):
    """line by line, wherever there's no real terminal"""
    def heard(row, kind):
        print(row, flush=True)
    chat.heard = heard
    chat.say("Type to talk to your AI. /help says the rest.", 2)
    while chat.on:
        try:
            text = input("> ")
        except (EOFError, KeyboardInterrupt):
            break
        chat.line(text)
        chat.pump()
        while chat.proc:
            time.sleep(0.05)
            chat.pump()


COLOUR = {0: "", 1: "\x1b[33m", 2: "\x1b[36m", 3: "\x1b[36m"}


class Screen:
    """the box on a real terminal. Every row is written at its own place, so nothing ever scrolls out of the safe box, and only the rows that changed are written, which is what keeps it from flickering on a tube"""

    def __init__(self, chat, left, top, dark, ascii_only):
        self.chat, self.left, self.top, self.dark_after, self.ascii = chat, left, top, dark, ascii_only
        self.typed, self.prev, self.dark, self.touched = "", [], False, time.time()

    def write(self, s):
        if self.ascii:
            s = s.translate(ASCII).encode("ascii", "replace").decode("ascii")
        os.write(1, s.encode("utf-8", "replace"))

    def draw(self):
        now = time.time()
        rows, cur = self.chat.frame(self.typed, now)
        out = []
        for i, (text, kind) in enumerate(rows):
            if i < len(self.prev) and self.prev[i] == (text, kind):
                continue
            out.append("\x1b[%d;%dH%s%s\x1b[0m" % (self.top + i + 1, self.left + 1, COLOUR[kind], text.ljust(self.chat.cols)))
        self.prev = rows
        out.append("\x1b[%d;%dH" % (self.top + len(rows), self.left + cur + 1))
        self.write("".join(out))

    def clear(self):
        self.prev = []
        self.write("\x1b[0m\x1b[2J\x1b[H")

    def key(self, data):
        c = self.chat
        page = max(1, c.rows - 3)
        # one read can hold several keys: a held PgUp, or fast typing
        for m in re.finditer(r"\x1b(?:\[[0-9;]*[A-Za-z~]|O[A-Za-z]|.)?|.", data, re.S):
            ch = m.group(0)
            name = KEYS.get(ch)
            if name:
                c.back = {"up": c.back + 1, "down": max(0, c.back - 1), "pgup": c.back + page, "pgdn": max(0, c.back - page), "end": 0}[name]
                c.changed = True
                continue
            if ch[0] == "\x1b":
                continue
            if ch in "\r\n":
                text, self.typed = self.typed, ""
                c.line(text)
            elif ch in "\x7f\x08":
                self.typed = self.typed[:-1]
            elif ch == "\x15":
                self.typed = ""
            elif ch == "\x04":
                c.on = False
            elif ch >= " ":
                self.typed += ch
            c.changed = True

    def run(self):
        import select
        import termios
        import tty
        fd = 0
        old = termios.tcgetattr(fd)
        back = lambda: (termios.tcsetattr(fd, termios.TCSADRAIN, old), self.clear())

        def take():   # back from a game, which counts as a touch: the time it took isn't the screen left alone
            tty.setcbreak(fd)
            self.clear()
            self.touched = time.time()
        self.chat.hand_over = back
        self.chat.take_back = take
        take()
        self.chat.say("Type to talk to your AI. /help says the rest.", 2)
        last = 0
        try:
            while self.chat.on:
                try:
                    r = select.select([fd], [], [], 0.1)[0]
                    now = time.time()
                    if r:
                        data = os.read(fd, 256).decode("utf-8", "replace")
                        self.touched = now
                        if self.dark:
                            self.dark = False
                            self.clear()
                            self.chat.changed = True
                        else:
                            self.key(data)
                    n0 = len(self.chat.lines)
                    self.chat.pump()
                    if len(self.chat.lines) != n0:
                        self.touched = now
                        if self.dark:
                            self.dark = False
                            self.clear()
                    if self.dark_after and not self.dark and now - self.touched > self.dark_after:
                        self.dark = True
                        self.clear()
                    if not self.dark and (self.chat.changed or int(now) != last):
                        self.chat.changed = False
                        last = int(now)
                        self.draw()
                except KeyboardInterrupt:
                    if self.chat.proc:
                        self.chat.stop()
                    else:
                        self.chat.on = False
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
            self.clear()


def on_tv():
    """a console on the composite output, and not on HDMI, where the whole screen can be read. A TV reports its connector as anything but disconnected, since it can't say"""
    if os.environ.get("TERM") != "linux":
        return False
    try:
        return any(open(p).read().strip() != "disconnected" for p in glob.glob("/sys/class/drm/*Composite*/status"))
    except OSError:
        return False


def tv_box(cols, rows):
    """the title-safe box in cells, moved by the player's screen fit: the same rule as the TV's grid in the engine, whose middle follows what the player said they could see"""
    w, h = min(35, int(cols * 0.8)), max(6, int(rows * 0.8))
    left, top = (cols - w) // 2, (rows - h) // 2
    for path in ("/boot/firmware/aigame_display.txt", "save/display.txt"):
        try:
            for ln in open(path):
                p = ln.split()
                if len(p) == 6 and p[0] == "edges" and p[1] == "480i":
                    l, r, t, b = (int(x) for x in p[2:])
                    left = max(0, min(cols - w, int((l + 720 - r) / 2 * cols / 720 - w / 2)))
                    top = max(0, min(rows - h, int((t + 480 - b) / 2 * rows / 480 - h / 2)))
            break
        except (OSError, ValueError):
            pass
    return w, h, left, top


def main():
    a = sys.argv[1:]
    if a == ["--selftest"]:
        return selftest()
    opt = {"--cols": 0, "--rows": 0, "--left": -1, "--top": -1, "--dark": -1}
    flags, play = set(), [("bash"), os.path.join(HERE, "play.sh")]
    while a and a[0].startswith("--"):
        k = a.pop(0)
        if k in opt:
            opt[k] = int(a.pop(0))
        elif k == "--play":
            play = a.pop(0).split()
        elif k in ("--plain", "--nocap"):
            flags.add(k)
        elif k == "--":
            break
        else:
            sys.exit("no option " + k)
    if not a:
        sys.exit(__doc__)
    real = os.name == "posix" and sys.stdin.isatty() and sys.stdout.isatty() and "--plain" not in flags
    tv = real and on_tv()
    if tv:
        for f in FONTS:
            if os.path.exists(f):
                subprocess.run(["setfont", f], stderr=subprocess.DEVNULL, check=False)
                break
    size = shutil.get_terminal_size()
    cols, rows, left, top = tv_box(size.columns, size.lines) if tv else (min(size.columns, 100), size.lines, 0, 0)
    cols, rows = opt["--cols"] or cols, opt["--rows"] or rows
    left, top = (opt["--left"] if opt["--left"] >= 0 else left), (opt["--top"] if opt["--top"] >= 0 else top)
    cap = "--nocap" not in flags and shutil.which("systemd-run") is not None
    chat = Chat(a, cols, rows, play, cap)
    if real:
        Screen(chat, left, top, opt["--dark"] if opt["--dark"] >= 0 else (300 if tv else 0), os.environ.get("TERM") == "linux").run()
    else:
        plain(chat)
    if chat.proc:
        chat.stop()
    return 0


def selftest():
    """what can be checked anywhere, then on a machine with ptys the real screen, read back by a small terminal of its own"""
    global LOG
    LOG = os.devnull
    bad = 0

    def check(what, ok):
        nonlocal bad
        print(("  ok   " if ok else "  FAIL ") + what)
        bad += not ok

    check("colours and cursor moves come off", clean(b"\x1b[1;31mred\x1b[0m \x1b]0;title\x07text\r\n") == "red text")
    check("a progress bar keeps its last state", clean(b"10%\r50%\r100%\n") == "100%")
    check("control characters come off", clean(b"a\x07b\x00c") == "abc")
    rows = wrap("one two three four five six seven eight nine ten", 12)
    check("wrapping stays in the box", all(len(r) <= 12 for r in rows) and " ".join(rows) == "one two three four five six seven eight nine ten")
    check("a word longer than the box is cut", all(len(r) <= 12 for r in wrap("x" * 30, 12)))
    check("a blank line stays a row", wrap("", 12) == [""])
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    c = Chat(["x"], 20, 8, [], False)
    c.log = None
    for i in range(30):
        c.say("line %d" % i)
    f, cur = c.frame("hello", 0.0)
    check("a frame is the box's height, the facts on top", len(f) == 8 and f[0][1] == 3)
    check("it follows the end", f[6][0] == "line 29")
    check("the cursor is after what's typed", cur == 7 and f[7][0] == "> hello")
    c.back = 5
    f, _ = c.frame("", 0.0)
    check("reading back shows earlier rows", f[6][0] == "line 24")
    c.say("line 30")
    f, _ = c.frame("", 0.0)
    check("and stays put when more comes", f[6][0] == "line 24" and c.back == 6)
    c.back = 9999
    f, _ = c.frame("", 0.0)
    check("reading back stops at the start", f[1][0] == "line 0")
    c.back = 0
    f, cur = c.frame("y" * 100, 0.0)
    check("a long message keeps its last three rows", len(f) == 8 and sum(1 for r in f if r[1] == 1) == 3 and all(len(r[0]) <= 20 for r in f))
    exact = "z" * 18
    f, cur = c.frame(exact, 0.0)
    check("a full row moves the cursor to the next", cur == 0 and f[-1][0] == "")

    # the whole program, line by line, with a stand-in for the AI
    agent = [sys.executable, "-c", "import sys,time;m=sys.argv[1];print('\\x1b[32mI heard:\\x1b[0m '+m,flush=True);time.sleep(3 if m=='slow' else 0);print('');print('word '*30);sys.exit(3 if m=='fail' else 0)", "{}"]
    game = "%s -c print('[play]-0-late')" % sys.executable
    script = "/help\nhello there\n/nope\nfail\n/play games/x 12 p.rpack\n/play\n/stop\n/quit\nnot reached\n"
    env = dict(os.environ, PYTHONIOENCODING="utf-8", HOME=os.path.join(HERE, "_nohome"), USERPROFILE=os.path.join(HERE, "_nohome"))
    p = subprocess.run([sys.executable, os.path.abspath(__file__), "--plain", "--nocap", "--cols", "30", "--rows", "12", "--play", game, "--"] + agent, input=script.encode(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=60)
    out = p.stdout.decode("utf-8", "replace")
    rows = [r[2:] if r.startswith("> ") else r for r in out.splitlines()]
    check("it hears the AI, in plain text", "I heard: hello there" in out and "\x1b" not in out)
    check("nothing it shows is wider than the box", all(len(r) <= 30 for r in rows))
    check("an unknown command says so", "There's no /nope" in out)
    check("a failed answer says how", "(it stopped, exit 3)" in out)
    check("/play runs the game, and /play again the same one", out.count("Playing games/x 12 p.rpack") == 2)
    check("what the game printed comes back into the conversation", out.count("[play]-0-late") == 2)
    check("/stop with nothing running says so", "Nothing's running." in out)
    check("/quit quits", "not reached" not in out and p.returncode == 0)
    os.makedirs("_tvbox/save", exist_ok=True)
    cwd = os.getcwd()
    os.chdir("_tvbox")
    check("a TV's box: the safe 35 x 12 of a 45 x 15 console", tv_box(45, 15) == (35, 12, 5, 1))
    open("save/display.txt", "w").write("mode 480i\nedges 480i 90 10 60 0\n")
    check("a TV's box: the player's screen fit moves it", tv_box(45, 15) == (35, 12, 7, 2))
    os.chdir(cwd)
    shutil.rmtree("_tvbox", ignore_errors=True)
    try:
        import pty
    except ImportError:
        pty = None
    if pty and os.name == "posix":
        pty_test(check, agent)
    else:
        print("  (no ptys here, so not the real screen)")
    print("all good" if not bad else "%d FAILED" % bad)
    return 1 if bad else 0


class Glass:
    """just enough of a terminal to read the screen back: cursor moves, clearing, and where every character landed"""

    def __init__(self):
        self.g, self.x, self.y, self.stray = {}, 0, 0, 0

    def feed(self, s):
        for m in re.finditer(r"\x1b\[(\d*);?(\d*)([A-Za-z])|\x1b.|([^\x1b])", s):
            if m.group(4):
                ch = m.group(4)
                if ch not in "\r\n":
                    self.g[(self.x, self.y)] = ch
                    self.x += 1
            elif m.group(3) == "H":
                self.y, self.x = int(m.group(1) or 1) - 1, int(m.group(2) or 1) - 1
            elif m.group(3) == "J":
                self.g.clear()

    def row(self, y, x0, w):
        return "".join(self.g.get((x, y), " ") for x in range(x0, x0 + w)).rstrip()

    def box(self, x0, y0, w, h):
        return [self.row(y, x0, w) for y in range(y0, y0 + h)]

    def outside(self, x0, y0, w, h):
        return [k for k, v in self.g.items() if v != " " and not (x0 <= k[0] < x0 + w and y0 <= k[1] < y0 + h)]


def pty_test(check, agent):
    import pty
    import select
    L, T, W, H = 5, 1, 35, 12
    glass = Glass()
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["HOME"] = os.path.join(HERE, "_nohome")
        os.execv(sys.executable, [sys.executable, os.path.abspath(__file__), "--nocap", "--cols", str(W), "--rows", str(H), "--left", str(L), "--top", str(T), "--dark", "2", "--play", "%s -c __import__('time').sleep(3);print('[play]-0-late')" % sys.executable, "--"] + agent)

    def settle(sec=0.5):
        end = time.time() + sec
        while time.time() < end:
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    glass.feed(os.read(fd, 65536).decode("utf-8", "replace"))
                except OSError:
                    return

    def has(text):
        return any(text in r for r in glass.box(L, T, W, H))

    def send(s):
        os.write(fd, s.encode())
        settle()
    settle(1.0)
    n0 = 0
    check("the screen: it says what to do", has("Type to talk"))
    check("the screen: the status row says it's ready", glass.row(T, L, W).startswith("READY"))
    send("hello")
    check("the screen: what you type shows in the box", has("> hello"))
    send("\x7f\x7f")
    check("the screen: backspace", has("> hel") and not has("> hello"))
    send("lo\r")
    settle(1.0)
    check("the screen: the answer comes", has("I heard: hello"))
    check("the screen: nothing ever lands outside the safe box", not glass.outside(L, T, W, H))
    send("/play\r")
    check("the screen: /play with nothing to play says how", has("/play <game folder>"))
    for i in range(6):
        send("again %d\r" % i)
    settle(1.0)
    before = glass.box(L, T, W, H)
    send("\x1b[5~")
    check("the screen: PgUp reads back, and the status says so", glass.box(L, T, W, H) != before and glass.row(T, L, W).startswith("BACK"))
    one = glass.box(L, T, W, H)
    send("\x1b[5~\x1b[5~")
    check("the screen: two keys that arrive together are both heard", glass.box(L, T, W, H) != one)
    send("\x1b[F")
    check("the screen: End comes back", glass.row(T, L, W).startswith("READY"))
    check("the screen: still nothing outside the box", not glass.outside(L, T, W, H))
    settle(2.6)
    check("the screen: left alone it goes dark", not has("READY") and not glass.g)
    send("x")
    check("the screen: any key brings it back, and isn't typed", has("READY") and not has("> x"))
    send("/play games/x 12 p.rpack\r")   # a game longer than the box's two seconds of being left alone
    settle(3.3)
    check("the screen: a game comes back into the conversation", has("[play]-0-late"))
    check("the screen: and coming back from a game isn't being left alone", has("READY"))
    send("slow\r")
    settle(1.0)
    check("the screen: while it answers, the status says it's working", glass.row(T, L, W).startswith("WORKING 0:0"))
    send("and this\r")
    check("the screen: a message sent meanwhile waits its turn", has("(after this answer)"))
    send("\x03")
    settle(0.8)
    check("the screen: Ctrl-C stops the answer and stays", has("(stopped)") and glass.row(T, L, W).startswith("READY"))
    send("/quit\r")
    settle(0.5)
    try:
        os.close(fd)
    except OSError:
        pass
    _, status = os.waitpid(pid, 0)
    check("the screen: /quit quits", os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0)


if __name__ == "__main__":
    sys.exit(main())
