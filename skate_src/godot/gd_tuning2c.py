#!/usr/bin/env python3
"""gd_tuning2c.py -- a Godot Resource script's exported numbers as a C struct and its defaults (11-12, the Godot proof of concept's port of
their skating from their current code: skate_tuning.gd). A step of the port maker: their numbers come across by machine, never retyped.

usage: gd_tuning2c.py <their tuning .gd> <out .inc> <StructName> <DEFAULTS_NAME>

Reads every `@export var NAME := VALUE` / `@export var NAME: TYPE = VALUE` (and @export_range forms): a whole number becomes an int, anything
else a float. The comment lines (##) above each go with it."""
import re
import sys

src, out, struct, defaults = sys.argv[1:5]
lines = open(src, encoding="utf-8").read().splitlines()
fields, notes = [], []
pat = re.compile(r"^@export(?:_[a-z_]+\([^)]*\))?\s+var\s+(\w+)\s*(?::\s*(\w+))?\s*:?=\s*([-+0-9.eE]+)\s*(?:#.*)?$")
for ln in lines:
    s = ln.strip()
    if s.startswith("##"):
        notes.append(s[2:].strip())
        continue
    m = pat.match(s)
    if m:
        name, typ, val = m.group(1), m.group(2), m.group(3)
        is_int = (typ == "int") or (typ is None and re.fullmatch(r"[-+]?\d+", val) is not None)
        fields.append((name, "int" if is_int else "float", val, " ".join(notes)))
    if not s.startswith("##"):
        notes = []
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("/* %s -- made by skate_src/godot/gd_tuning2c.py from %s: don't edit, make it again. */\n" % (out.replace("\\", "/").split("/")[-1], src.replace("\\", "/")))
    f.write("typedef struct {\n")
    for name, t, val, note in fields:
        f.write("    %s %s;%s\n" % (t, name, ("   /* %s */" % note.replace("*/", "* /")) if note else ""))
    f.write("} %s;\n" % struct)
    f.write("static const %s %s = {\n" % (struct, defaults))
    for name, t, val, note in fields:
        v = val if t == "int" else (val if any(c in val for c in ".eE") else val + ".0") + "f"
        f.write("    .%s = %s,\n" % (name, v))
    f.write("};\n")
print("%s: %d numbers into %s" % (src, len(fields), out))
