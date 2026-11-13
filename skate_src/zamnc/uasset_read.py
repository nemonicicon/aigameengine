"""uasset_read.py -- read an Unreal package off the disk. No Unreal, no run, nothing written.

    python uasset_read.py <file.uasset|.umap>            the exports and their classes
    python uasset_read.py <file> --props <export name>   one export's tagged properties
    python uasset_read.py <file> --graph                 every graph node: pins, defaults, links

11-02: a project doesn't have to be run to be read. A package is a plain
binary: a name table, an import table, an export table, and each export's tagged properties.
Blueprint graphs are in there too, because an editor package keeps its K2Nodes and their pins.

Written for what the zombie prototype's UE 5.7 saved (file version UE5 1018, UE4 522), and it REFUSES what it
cannot walk instead of guessing: every property has a size in its tag, so a value that does not
land exactly on its tag's end is counted in Package.misses, and a node whose pins do not land on
the end of its export is counted in Package.pin_misses. Both are the receipt that the read is
right -- a parser that drifts cannot land on every boundary in a file by accident. A value with
no reader is skipped by its size and listed in Package.unread: nothing after it drifts, and
nobody can mistake it for a value we have.
"""
import struct, sys

TAG = 0x9E2A83C1
PKG_FILTER_EDITOR_ONLY = 0x80000000

# EPropertyTagFlags
F_ARRAY_INDEX, F_GUID, F_EXT, F_BINARY, F_BOOL_TRUE = 1, 2, 4, 8, 0x10


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def take(self, n):
        if self.p + n > len(self.d) or n < 0:
            raise ValueError("read of %d at %d runs off %d" % (n, self.p, len(self.d)))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def _u(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))

    def i8(self): return self._u("b")[0]
    def u8(self): return self._u("B")[0]
    def i16(self): return self._u("h")[0]
    def u16(self): return self._u("H")[0]
    def i32(self): return self._u("i")[0]
    def u32(self): return self._u("I")[0]
    def i64(self): return self._u("q")[0]
    def u64(self): return self._u("Q")[0]
    def f32(self): return self._u("f")[0]
    def f64(self): return self._u("d")[0]
    def guid(self): return self.take(16).hex()

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            if n > 1 << 20:
                raise ValueError("string of %d" % n)
            return self.take(n)[:-1].decode("latin-1")
        n = -n
        if n > 1 << 20:
            raise ValueError("string of %d" % n)
        return self.take(n * 2)[:-2].decode("utf-16-le", "replace")


class Package:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.d = f.read()
        self.misses = []        # [(where, type, wanted end, landed)]
        self.unread = []        # [(where, type, why)]: skipped by size, no reader for the value
        self.pin_misses = []
        self._summary()
        self._tables()

    # ---- the header -----------------------------------------------------------------
    def _summary(self):
        r = Reader(self.d)
        if r.u32() != TAG:
            raise ValueError("%s: not an Unreal package" % self.path)
        self.legacy = r.i32()
        if self.legacy != -4:
            r.i32()
        self.ue4 = r.i32()
        self.ue5 = r.i32() if self.legacy <= -8 else 0
        r.i32()
        if self.ue5 >= 1016:
            r.take(20)
            self.header = r.i32()
        ncv = r.i32()
        self.customver = {}
        for _ in range(ncv):
            g = r.guid()
            self.customver[g] = r.i32()
        if self.ue5 < 1016:
            self.header = r.i32()
        self.name = r.fstr()
        self.flags = r.u32()
        self.nname, self.oname = r.i32(), r.i32()
        self.nsoft, self.osoft = 0, 0
        if self.ue5 >= 1008:
            self.nsoft, self.osoft = r.i32(), r.i32()
        if not self.flags & PKG_FILTER_EDITOR_ONLY:
            r.fstr()
        r.i32(), r.i32()
        self.nexp, self.oexp = r.i32(), r.i32()
        self.nimp, self.oimp = r.i32(), r.i32()

    def _tables(self):
        r = Reader(self.d, self.oname)
        self.names = []
        for _ in range(self.nname):
            self.names.append(r.fstr())
            r.u32()

        r = Reader(self.d, self.oimp)
        self.imports = []
        for _ in range(self.nimp):
            e = {"class_package": self.fname(r), "class": self.fname(r), "outer": r.i32(),
                 "name": self.fname(r)}
            if self.ue4 >= 520:
                e["package"] = self.fname(r)
            if self.ue5 >= 1003:
                r.i32()
            self.imports.append(e)

        r = Reader(self.d, self.oexp)
        self.exports = []
        for i in range(self.nexp):
            e = {"index": i + 1, "class_i": r.i32(), "super_i": r.i32(), "template_i": r.i32(),
                 "outer": r.i32(), "name": self.fname(r), "flags": r.u32(),
                 "size": r.i64(), "off": r.i64()}
            r.take(12)
            if self.ue5 < 1005:
                r.take(16)
            if self.ue5 >= 1006:
                e["inherited"] = r.i32()
            r.u32()
            r.i32()
            e["is_asset"] = r.i32()
            if self.ue5 >= 1003:
                r.i32()
            r.take(20)
            e["script_start"], e["script_end"] = 0, e["size"]
            if self.ue5 >= 1010:
                e["script_start"], e["script_end"] = r.i64(), r.i64()
            self.exports.append(e)

        self.softpaths = []
        if self.nsoft:
            r = Reader(self.d, self.osoft)
            for _ in range(self.nsoft):
                self.softpaths.append(self._softpath_raw(r))

    # ---- names and references --------------------------------------------------------
    def fname(self, r):
        i, n = r.i32(), r.i32()
        if not 0 <= i < len(self.names):
            raise ValueError("name index %d of %d" % (i, len(self.names)))
        s = self.names[i]
        return s + "_%d" % (n - 1) if n else s

    def obj_name(self, i):
        if i > 0:
            return self.exports[i - 1]["name"]
        if i < 0:
            return self.imports[-i - 1]["name"]
        return None

    def obj_path(self, i):
        """The full path of a package index: /Game/x/BP_Y.BP_Y_C or a local export's chain."""
        if i == 0:
            return None
        chain = []
        while i:
            if i > 0:
                e = self.exports[i - 1]
            else:
                e = self.imports[-i - 1]
            chain.append(e["name"])
            i = e["outer"]
        chain.reverse()
        if not chain[0].startswith("/"):
            chain.insert(0, self.name)
        out = chain[0]
        for k, c in enumerate(chain[1:]):
            out += ("." if k == 0 else ":" if k == 1 else ".") + c
        return out

    def class_of(self, e):
        return self.obj_name(e["class_i"]) or "Class"

    def class_path(self, e):
        return self.obj_path(e["class_i"])

    def find(self, name):
        return [e for e in self.exports if e["name"] == name]

    def children(self, i):
        return [e for e in self.exports if e["outer"] == i]

    # ---- property tags ---------------------------------------------------------------
    def _type(self, r):
        n, c = self.fname(r), r.i32()
        if not 0 <= c < 16:
            raise ValueError("type %s with %d params" % (n, c))
        return (n, [self._type(r) for _ in range(c)])

    def props(self, e):
        """An export's tagged properties, as {name: value} (a static array index -> name[i])."""
        if e["script_end"] <= e["script_start"]:
            return {}
        r = Reader(self.d, e["off"] + e["script_start"])
        if self.ue5 >= 1011 and r.u8() & 2:     # an object's tags open with a control byte
            r.u8()
        return self._tagged(r, e["off"] + e["script_end"], e["name"])

    def _tagged(self, r, limit, where):
        out = {}
        while r.p < limit:
            name = self.fname(r)
            if name == "None":
                break
            ty = self._type(r)
            size = r.i32()
            flags = r.u8()
            idx = r.i32() if flags & F_ARRAY_INDEX else 0
            if flags & F_GUID:
                r.take(16)
            if flags & F_EXT:
                if r.u8() & 2:
                    r.u8()
                    r.i32()
            end = r.p + size
            try:
                v = bool(flags & F_BOOL_TRUE) if ty[0] == "BoolProperty" else \
                    self._item(r, ty, end, where + "." + name, flags & F_BINARY)
            except (ValueError, struct.error, UnicodeError) as x:
                # bounded by its tag, so nothing after it drifts -- but it IS a value we do not have
                self.unread.append((where + "." + name, self._tyname(ty), str(x)))
                v = {"_unread": ty[0], "_why": str(x)}
                r.p = end
            if r.p != end:
                self.misses.append((where + "." + name, self._tyname(ty), end, r.p))
                r.p = end
            out[name if not idx else "%s[%d]" % (name, idx)] = v
        return out

    def _tyname(self, ty):
        return ty[0] + ("(" + ",".join(self._tyname(t) for t in ty[1]) + ")" if ty[1] else "")

    def _item(self, r, ty, end, where, binary=None):
        k = ty[0]
        if k == "BoolProperty":
            return bool(r.u8())
        if k == "IntProperty": return r.i32()
        if k == "Int8Property": return r.i8()
        if k == "Int16Property": return r.i16()
        if k == "Int64Property": return r.i64()
        if k == "UInt16Property": return r.u16()
        if k == "UInt32Property": return r.u32()
        if k == "UInt64Property": return r.u64()
        if k == "FloatProperty": return round(r.f32(), 6)
        if k == "DoubleProperty": return r.f64()
        if k == "NameProperty": return self.fname(r)
        if k == "StrProperty": return r.fstr()
        if k == "TextProperty": return self._text(r)
        if k == "EnumProperty": return self.fname(r)
        if k == "ByteProperty":
            return self.fname(r) if end - r.p >= 8 and ty[1] and ty[1][0][0] != "None" else r.u8()
        if k in ("ObjectProperty", "ClassProperty", "WeakObjectProperty", "LazyObjectProperty",
                 "InterfaceProperty", "ObjectPtrProperty", "ClassPtrProperty"):
            if k == "LazyObjectProperty":
                return r.guid()
            return self.obj_path(r.i32())
        if k in ("SoftObjectProperty", "SoftClassProperty"):
            return self._softpath(r)
        if k == "StructProperty":
            sname = ty[1][0][0] if ty[1] else ""
            return self._struct(r, sname, end, where, binary)
        if k == "ArrayProperty":
            n = r.i32()
            inner = ty[1][0]
            if inner[0] == "StructProperty":
                sname = inner[1][0][0] if inner[1] else ""
                return [self._struct(r, sname, end, "%s[%d]" % (where, i), None) for i in range(n)]
            return [self._item(r, inner, end, where) for i in range(n)]
        if k == "SetProperty":
            r.i32()
            n = r.i32()
            return [self._item(r, ty[1][0], end, where) for i in range(n)]
        if k == "MapProperty":
            r.i32()
            n = r.i32()
            return [[self._item(r, ty[1][0], end, where), self._item(r, ty[1][1], end, where)]
                    for i in range(n)]
        raise ValueError("no reader for " + self._tyname(ty))

    def _text(self, r):
        r.u32()
        h = r.i8()
        if h == -1:
            return r.fstr() if r.i32() else ""
        if h == 0:
            r.fstr(), r.fstr()
            return r.fstr()
        if h == 11:
            self.fname(r)
            return r.fstr()
        raise ValueError("text history %d" % h)

    def _softpath_raw(self, r):
        pkg, asset = self.fname(r), self.fname(r)
        sub = r.fstr()
        p = "" if pkg == "None" else pkg
        if asset != "None":
            p += "." + asset
        if sub:
            p += ":" + sub
        return p

    def _softpath(self, r):
        if self.softpaths:
            return self.softpaths[r.i32()]
        return self._softpath_raw(r)

    # the structs that serialize themselves instead of writing tags
    NATIVE = {
        "Vector": lambda s, r: dict(zip("xyz", r._u("3d"))),
        "Rotator": lambda s, r: dict(zip(("pitch", "yaw", "roll"), r._u("3d"))),
        "Quat": lambda s, r: dict(zip("xyzw", r._u("4d"))),
        "Vector2D": lambda s, r: dict(zip("xy", r._u("2d"))),
        "Vector4": lambda s, r: dict(zip("xyzw", r._u("4d"))),
        "Plane": lambda s, r: dict(zip("xyzw", r._u("4d"))),
        "Vector3f": lambda s, r: dict(zip("xyz", r._u("3f"))),
        "Vector2f": lambda s, r: dict(zip("xy", r._u("2f"))),
        "LinearColor": lambda s, r: dict(zip("rgba", (round(v, 6) for v in r._u("4f")))),
        "Color": lambda s, r: dict(zip("bgra", r._u("4B"))),
        "IntPoint": lambda s, r: dict(zip("xy", r._u("2i"))),
        "IntVector": lambda s, r: dict(zip("xyz", r._u("3i"))),
        "Guid": lambda s, r: r.guid(),
        "Box": lambda s, r: {"min": r._u("3d"), "max": r._u("3d"), "valid": r.u8()},
        "Box2D": lambda s, r: {"min": r._u("2d"), "max": r._u("2d"), "valid": r.u8()},
        "BoxSphereBounds": lambda s, r: {"origin": r._u("3d"), "extent": r._u("3d"), "radius": r.f64()},
        "DateTime": lambda s, r: r.i64(),
        "Timespan": lambda s, r: r.i64(),
        "FrameNumber": lambda s, r: r.i32(),
        "SoftObjectPath": lambda s, r: s._softpath(r),
        "SoftClassPath": lambda s, r: s._softpath(r),
        "PerPlatformFloat": lambda s, r: (r.i32() and None) or r.f32(),
        "PerPlatformInt": lambda s, r: (r.i32() and None) or r.i32(),
        "PerPlatformBool": lambda s, r: (r.i32() and None) or r.i32(),
        "GameplayTagContainer": lambda s, r: [s.fname(r) for _ in range(r.i32())],
        "EdGraphPinType": lambda s, r: s._pin_type(r),
    }

    def _struct(self, r, sname, end, where, binary):
        f = self.NATIVE.get(sname)
        if f and binary is not False:
            return f(self, r)
        if binary:
            # binary-serialized and not one of ours: the tag's size is all we can trust
            raw = r.take(end - r.p)
            return {"_struct": sname, "_raw": raw[:64].hex(), "_len": len(raw)}
        return self._tagged(r, end, where)

    # ---- the graphs ------------------------------------------------------------------
    def node_pins(self, e):
        """The pins of one graph node: what follows its tags in the export."""
        r = Reader(self.d, e["off"] + e["script_end"])
        end = e["off"] + e["size"]
        if r.i32():                     # UObject's HasGuid
            r.take(16)
        pins = self._pin_array_owned(r)
        # what a node class writes after its pins
        c = self.class_of(e)
        if r.p != end and c in self.EDITABLE_PINS:
            for _ in range(self._count(r)):     # its user-defined pins: name, type, side, default
                self.fname(r), self._pin_type(r), r.u8(), r.fstr()
        if r.p != end and c in ("K2Node_DynamicCast", "K2Node_ClassDynamicCast"):
            r.u8()                              # pure or not
        if r.p != end:
            self.pin_misses.append((c, e["name"], end - r.p))
        return pins

    EDITABLE_PINS = ("K2Node_Event", "K2Node_CustomEvent", "K2Node_FunctionEntry",
                     "K2Node_FunctionResult", "K2Node_Tunnel", "K2Node_MacroInstance",
                     "K2Node_Composite", "K2Node_ComponentBoundEvent", "K2Node_ActorBoundEvent")

    def _pin_array_owned(self, r):
        return [self._pin_owned(r) for _ in range(self._count(r))]

    def _count(self, r):
        n = r.i32()
        if not 0 <= n < 4096:
            raise ValueError("pin array of %d" % n)
        return n

    def _pin_ref(self, r):
        if r.i32():
            return None
        return (r.i32(), r.guid())

    def _pin_owned(self, r):
        if r.i32():
            return None
        r.i32(), r.take(16)             # the reference that owns it: node, pin id
        p = {"node": r.i32(), "id": r.guid(), "name": self.fname(r)}
        p["friendly"] = self._text(r)
        p["source_index"] = r.i32()
        r.fstr()                        # tooltip
        p["dir"] = "out" if r.u8() else "in"
        p["type"] = self._pin_type(r)
        p["default"] = r.fstr()
        p["auto"] = r.fstr()
        p["default_obj"] = self.obj_path(r.i32())
        p["default_text"] = self._text(r)
        p["links"] = [self._pin_ref(r) for _ in range(self._count(r))]
        p["subpins"] = [self._pin_ref(r) for _ in range(self._count(r))]
        p["parent"] = self._pin_ref(r)
        p["passthrough"] = self._pin_ref(r)
        r.take(16)                      # persistent guid
        r.u32()                         # bit field: hidden, not connectable, ...
        return p

    def _pin_type(self, r):
        t = {"cat": self.fname(r), "sub": self.fname(r), "obj": self.obj_path(r.i32()),
             "container": r.u8()}
        if t["container"] == 3:         # a map's value type
            t["val"] = (self.fname(r), self.fname(r), self.obj_path(r.i32()))
            r.i32(), r.i32()
            r.i32()
        r.i32(), r.i32()                # by reference, weak
        r.i32(), self.fname(r), r.take(16)      # member reference: parent, name, guid
        r.i32()                         # const
        r.i32()                         # UObject wrapper
        r.i32()                         # single precision
        return t


# ---- command line --------------------------------------------------------------------
def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 2
    pk = Package(a[0])
    print("%s  UE4 %d UE5 %d  flags %08x  %d names, %d imports, %d exports, %d bytes" % (
        pk.name, pk.ue4, pk.ue5, pk.flags, pk.nname, pk.nimp, pk.nexp, len(pk.d)))
    if "--props" in a:
        import json
        for e in pk.find(a[a.index("--props") + 1]):
            print(json.dumps(pk.props(e), indent=1, default=str))
    elif "--graph" in a:
        for e in pk.exports:
            if pk.class_of(e).startswith("K2Node") or pk.class_of(e).startswith("EdGraphNode"):
                print("#%d %s (%s)" % (e["index"], e["name"], pk.class_of(e)))
                for p in pk.node_pins(e):
                    if p:
                        print("   %s %-22s %-10s default=%r links=%d" % (
                            p["dir"], p["name"], p["type"]["cat"], p["default"], len(p["links"])))
    else:
        for e in pk.exports:
            print("%4d outer=%-4d %-34s %-40s %8d" % (
                e["index"], e["outer"], pk.class_of(e)[:34], e["name"][:40], e["size"]))
    if pk.misses:
        print("MISSES:", len(pk.misses), pk.misses[:10])
    if pk.pin_misses:
        print("PIN MISSES:", len(pk.pin_misses), pk.pin_misses[:10])
    return 0


if __name__ == "__main__":
    sys.exit(main())
