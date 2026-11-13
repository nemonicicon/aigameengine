#!/usr/bin/env python3
"""godot_shader.py -- a Godot spatial shader made into a surface shader the engine runs (11-12, the Godot proof of concept's step 12:
one of their levels draws its hedges and ground with shaders of its own, and no plain material carries what those draw).

What the engine runs is its own world fragment shader (reva_native/shaders/mesh.frag) built with GODOT_HOOK round the game's code, which this
file translates. Their code says what each pixel is made of (ALBEDO, NORMAL, ROUGHNESS, METALLIC, SPECULAR, AO, EMISSION, NORMAL_MAP); the
engine's sun, lights, shadows, sky and fog light it, as they light everything else. So:
  - Godot's shading language is close to GLSL: their functions, consts, structs and expressions come over as they are, every name of theirs
    at the top level renamed gd_<name> so nothing meets the engine's, and a local named for a GLSL keyword renamed <name>_.
  - a uniform is set at the cook: a number, a vector or a colour is a const with the material's value (a source_color colour made linear, as
    Godot does), and a texture is a binding of set 3 with the sampler its hints ask for (filter_*, repeat_*, source_color as sRGB).
  - vertex() runs for each pixel, on the interpolated vertex: the engine's prop is in the world already, so MODEL_MATRIX is the identity and a
    varying worked out from VERTEX, NORMAL and UV is what Godot would have interpolated (exactly, for what's affine in them).
  - fragment()'s VERTEX, NORMAL, TANGENT, BINORMAL and VIEW are in view space as Godot's are, in metres, from the engine's lens (taken as
    level: a lens rolled about its own axis isn't).
  - where the engine's world sits against theirs: godot2set.py lifted the scene and scaled it 1.75 to a metre, so the scene's own coordinates
    are vW / 1.75 less the lift.
What isn't carried says so and the shader isn't made (the surface is drawn with its plain material): light(), a vertex function that moves
its vertices, the screen's or the depth's texture, view and projection matrices, samplers other than 2D and 2D arrays, transparency.

usage: godot_shader.py <export folder> <name> <out folder> [--lift M]   cooks every shader the export's <name>.json carries (a check by hand);
       godot2set.py calls cook() for the shaders a park uses."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MESH_FRAG = os.path.normpath(os.path.join(HERE, "..", "..", "reva_native", "shaders", "mesh.frag"))
SKY_FRAG = os.path.normpath(os.path.join(HERE, "..", "..", "reva_native", "shaders", "skydome.frag"))
UPM = 1.75


class Unsupported(Exception):
    pass


TOK = re.compile(r"""(?P<ws>\s+)|(?P<lc>//[^\n]*)|(?P<bc>/\*.*?\*/)|(?P<pp>\#[^\n]*)|
    (?P<num>0[xX][0-9a-fA-F]+[uU]?|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuU]?)|(?P<id>[A-Za-z_]\w*)|(?P<str>"[^"\n]*")|
    (?P<op><<=|>>=|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\^\^|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[{}()\[\];,.:?=+\-*/%<>!~&|^])""", re.S | re.X)

SAMPLERS = {"sampler2D": 0, "sampler2DArray": 1}
UNSUPPORTED_SAMPLERS = {"sampler3D", "samplerCube", "samplerCubeArray", "isampler2D", "usampler2D", "isampler2DArray", "usampler2DArray", "isampler3D",
                        "usampler3D", "samplerExternalOES"}
# words GLSL 4.50 keeps for itself that Godot's language lets a shader name a local
GLSL_RESERVED = set("""attribute sample patch buffer shared coherent volatile restrict readonly writeonly atomic_uint layout centroid subroutine invariant
precise common partition active asm class union enum typedef template this resource goto inline noinline public static extern external interface long
short half fixed unsigned superp input output hvec2 hvec3 hvec4 fvec2 fvec3 fvec4 filter sizeof cast namespace using double dvec2 dvec3 dvec4 dmat2 dmat3
dmat4 image1D image2D image3D imageCube main""".split())
# what Godot's stages give, as the engine has it. Read-only expressions first (anywhere), then each stage's own
ANYWHERE = {"TIME": "V.misc.w", "PI": "GD_PI", "TAU": "GD_TAU", "E": "GD_E", "OUTPUT_IS_SRGB": "false", "CAMERA_POSITION_WORLD": "gdEye()",
            "CAMERA_DIRECTION_WORLD": "normalize(V.fwd.xyz)", "NODE_POSITION_WORLD": "vec3(0.0)", "MODEL_MATRIX": "mat4(1.0)", "MODEL_NORMAL_MATRIX": "mat3(1.0)",
            "INSTANCE_ID": "0", "VERTEX_ID": "0", "VIEW_INDEX": "0", "EYE_OFFSET": "vec3(0.0)", "VIEWPORT_SIZE": "V.cl3.xy", "CAMERA_VISIBLE_LAYERS": "0xffffffffu",
            "FRAGCOORD": "gl_FragCoord", "FRONT_FACING": "true", "POINT_COORD": "vec2(0.0)"}
NEVER = {"VIEW_MATRIX", "INV_VIEW_MATRIX", "PROJECTION_MATRIX", "INV_PROJECTION_MATRIX", "MODELVIEW_MATRIX", "MODELVIEW_NORMAL_MATRIX", "MAIN_CAM_INV_VIEW_MATRIX",
         "VIEW_MONO_LEFT", "VIEW_RIGHT", "POSITION", "DEPTH", "BONE_INDICES", "BONE_WEIGHTS", "SCREEN_TEXTURE", "DEPTH_TEXTURE", "NORMAL_ROUGHNESS_TEXTURE"}
V_MEMBERS = [("vec3", "VERTEX"), ("vec3", "NORMAL"), ("vec3", "TANGENT"), ("vec3", "BINORMAL"), ("vec2", "UV"), ("vec2", "UV2"), ("vec4", "COLOR"),
             ("float", "POINT_SIZE"), ("float", "ROUGHNESS"), ("vec4", "CUSTOM0"), ("vec4", "CUSTOM1"), ("vec4", "CUSTOM2"), ("vec4", "CUSTOM3"),
             ("vec4", "INSTANCE_CUSTOM")]
F_MEMBERS = [("vec3", "VERTEX"), ("vec3", "LIGHT_VERTEX"), ("vec3", "NORMAL"), ("vec3", "TANGENT"), ("vec3", "BINORMAL"), ("vec3", "VIEW"), ("vec2", "UV"),
             ("vec2", "UV2"), ("vec4", "COLOR"), ("vec2", "SCREEN_UV"), ("vec3", "ALBEDO"), ("float", "ALPHA"), ("float", "ALPHA_SCISSOR_THRESHOLD"),
             ("float", "ALPHA_HASH_SCALE"), ("float", "ALPHA_ANTIALIASING_EDGE"), ("vec2", "ALPHA_TEXTURE_COORDINATE"), ("float", "PREMUL_ALPHA_FACTOR"),
             ("float", "METALLIC"), ("float", "SPECULAR"), ("float", "ROUGHNESS"), ("float", "RIM"), ("float", "RIM_TINT"), ("float", "CLEARCOAT"),
             ("float", "CLEARCOAT_ROUGHNESS"), ("float", "ANISOTROPY"), ("vec2", "ANISOTROPY_FLOW"), ("float", "SSS_STRENGTH"),
             ("vec4", "SSS_TRANSMITTANCE_COLOR"), ("float", "SSS_TRANSMITTANCE_DEPTH"), ("float", "SSS_TRANSMITTANCE_BOOST"), ("vec3", "BACKLIGHT"),
             ("float", "AO"), ("float", "AO_LIGHT_AFFECT"), ("vec3", "EMISSION"), ("vec4", "FOG"), ("vec4", "RADIANCE"), ("vec4", "IRRADIANCE"),
             ("vec3", "NORMAL_MAP"), ("float", "NORMAL_MAP_DEPTH"), ("vec3", "BENT_NORMAL_MAP")]
# what's said and drawn as nothing (the engine has no such light): a shader that writes one still runs, and the note says so
NOT_DRAWN = {"RIM", "CLEARCOAT", "ANISOTROPY", "SSS_STRENGTH", "BACKLIGHT", "FOG", "RADIANCE", "IRRADIANCE", "BENT_NORMAL_MAP", "AO_LIGHT_AFFECT"}


def tokens(code):
    out, i = [], 0
    while i < len(code):
        m = TOK.match(code, i)
        if not m:
            raise Unsupported("a character the shading language hasn't got, at %r" % code[i:i + 20])
        k = m.lastgroup
        if k not in ("lc", "bc"):
            out.append((k, m.group(k)))
        else:
            out.append(("ws", " "))
        i = m.end()
    return out


def items(toks):
    """the top level, item by item: each a list of tokens, whitespace kept"""
    out, cur, depth = [], [], 0
    for t in toks:
        cur.append(t)
        if t[0] == "pp" and depth == 0:
            out.append(cur)
            cur = []
            continue
        if t[1] in ("{", "(", "["):
            depth += 1
        elif t[1] in ("}", ")", "]"):
            depth -= 1
            if depth == 0 and t[1] == "}":
                head = [x[1] for x in cur if x[0] != "ws"]
                if head and head[0] not in ("struct", "const", "uniform", "varying") and "=" not in head[:4]:
                    out.append(cur)   # a function's body has ended
                    cur = []
        elif t[1] == ";" and depth == 0:
            out.append(cur)
            cur = []
    if any(x[0] != "ws" for x in cur):
        raise Unsupported("the shader ends part way through something")
    return [it for it in out if any(x[0] != "ws" for x in it)]


def words(it):
    return [x[1] for x in it if x[0] != "ws"]


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def fnum(x):
    s = repr(float(x))
    return s if ("e" in s or "." in s or "inf" in s or "nan" in s) else s + ".0"


def value_glsl(typ, v, hints):
    """a material's value for a uniform of this type, as GLSL"""
    base = typ.rstrip("0123456789")
    n = int(typ[-1]) if typ[-1].isdigit() and base in ("vec", "ivec", "uvec", "bvec") else 1
    if "num" in v:
        x = v["num"]
        if typ == "bool":
            return "true" if x else "false"
        if typ in ("int",):
            return str(int(x))
        if typ == "uint":
            return "%du" % int(x)
        if typ == "float":
            return fnum(x)
        raise Unsupported("uniform of type %s given a number" % typ)
    comps = v.get("color") or v.get("vec")
    if comps is None:
        raise Unsupported("uniform of type %s given %s" % (typ, list(v)))
    comps = list(comps)
    if "color" in v and "source_color" in hints:
        comps = [srgb_to_linear(c) for c in comps[:3]] + comps[3:]
    if base == "vec":
        return "%s(%s)" % (typ, ", ".join(fnum(c) for c in comps[:n]))
    if base == "ivec":
        return "%s(%s)" % (typ, ", ".join(str(int(c)) for c in comps[:n]))
    if base == "uvec":
        return "%s(%s)" % (typ, ", ".join("%du" % int(c) for c in comps[:n]))
    if base == "bvec":
        return "%s(%s)" % (typ, ", ".join("true" if c else "false" for c in comps[:n]))
    raise Unsupported("uniform of type %s" % typ)


def tex_flags(kind, hints):
    f = 0
    if "source_color" in hints:
        f |= 1
    filt = [h for h in hints if h.startswith("filter_")]
    if filt:
        h = filt[0]
        if h.startswith("filter_nearest"):
            f |= 2
        if "mipmap" not in h:
            f |= 8
        if "anisotropic" in h:
            f |= 16
    if "repeat_disable" in hints:
        f |= 4
    if kind == 1:
        f |= 32
    return f


def default_texel(hints):
    if "hint_default_black" in hints:
        return (0, 0, 0, 255)
    if "hint_default_transparent" in hints:
        return (0, 0, 0, 0)
    if "hint_normal" in hints:
        return (128, 128, 255, 255)
    if "hint_anisotropy" in hints:
        return (255, 128, 0, 255)
    return (255, 255, 255, 255)


def parse_uniform(w):
    """'uniform' type name [: hints] [= default]: (type, name, array, hints, default tokens)"""
    i = 1
    if w[i] in ("lowp", "mediump", "highp"):
        i += 1
    typ = w[i]
    i += 1
    arr = None
    if w[i] == "[":
        arr = w[i + 1]
        i += 3
    name = w[i]
    i += 1
    if i < len(w) and w[i] == "[":
        arr = w[i + 1]
        i += 3
    hints, default = [], None
    if i < len(w) and w[i] == ":":
        i += 1
        depth, cur = 0, []
        while i < len(w) and not (depth == 0 and w[i] in ("=", ";")):
            if w[i] == "(":
                depth += 1
            elif w[i] == ")":
                depth -= 1
            if depth == 0 and w[i] == ",":
                hints.append("".join(cur))
                cur = []
            else:
                cur.append(w[i])
            i += 1
        if cur:
            hints.append("".join(cur))
    hints = [h.split("(")[0] for h in hints]
    if i < len(w) and w[i] == "=":
        default = w[i + 1:-1]
    return typ, name, arr, hints, default


def translate(rec, export_dir):
    """the hook's GLSL for one shader material, and what it needs: (glsl, textures, notes, has_discard).
    textures: [(binding, flags, [files] or None, default texel)]"""
    code = rec["code"]
    params = rec.get("params", {})
    notes = list(rec.get("notes", []))
    toks = tokens(code)
    its = items(toks)
    shader_type, render = None, []
    uniforms, varyings, consts, structs, funcs, globals_ = [], [], [], [], {}, set()
    for it in its:
        w = words(it)
        if not w:
            continue
        if it[0][0] == "pp" or w[0].startswith("#"):
            line = w[0]
            if line.startswith("#include"):
                raise Unsupported("it #includes another file (%s)" % line)
            consts.append(it)   # a #define and its kin go over as they are
            continue
        if w[0] == "shader_type":
            shader_type = w[1]
        elif w[0] == "render_mode":
            render = [x for x in w[1:-1] if x != ","]
        elif w[0] in ("group_uniforms",):
            continue
        elif w[0] == "global" and len(w) > 1 and w[1] == "uniform":   # the project's own value, as it was when the scene was exported
            u = parse_uniform(w[1:])
            uniforms.append(u)
            globals_.add(u[1])
        elif w[0] == "instance" and len(w) > 1 and w[1] == "uniform":
            raise Unsupported("an instance uniform (%s)" % " ".join(w[:5]))
        elif w[0] == "uniform":
            uniforms.append(parse_uniform(w))
        elif w[0] == "varying":
            varyings.append(it)
        elif w[0] == "const":
            consts.append(it)
        elif w[0] == "struct":
            structs.append(it)
        else:
            if "(" not in w:
                raise Unsupported("something at the top level it can't read: %s" % " ".join(w[:8]))
            name = w[w.index("(") - 1]
            funcs[name] = it
    if shader_type not in ("spatial", "sky"):
        raise Unsupported("a %s shader (spatial and sky ones are carried)" % shader_type)
    if shader_type == "spatial":
        for r in render:
            if r.startswith("blend_") and r != "blend_mix":
                raise Unsupported("render_mode %s" % r)
            if r in ("depth_draw_always", "depth_test_disabled", "world_vertex_coords", "skip_vertex_transform"):
                notes.append("render_mode %s: not carried" % r)
        if "light" in funcs:
            raise Unsupported("it has a light() function, which the engine's lights don't run")
        if "fragment" not in funcs:
            raise Unsupported("no fragment() function")
    elif "sky" not in funcs:
        raise Unsupported("no sky() function")
    for r in render:
        if shader_type == "sky" and r.startswith("use_"):
            notes.append("render_mode %s: drawn at the picture's own size, once" % r)
    # ---- what's renamed: every name at the top level
    tops = set(funcs) - {"vertex", "fragment", "sky"}
    for typ, name, arr, hints, default in uniforms:
        tops.add(name)
    for it in varyings + consts + structs:
        w = words(it)
        if w[0] == "struct":
            tops.add(w[1])
        elif w[0] == "varying" or w[0] == "const":
            ws = [x for x in w[1:] if x not in ("flat", "smooth", "lowp", "mediump", "highp", "const")]
            tops.add(ws[1] if ws[0] != "[" else ws[1])
        elif w[0].startswith("#define"):
            pass
    # ---- uniforms: textures to bindings, the rest to consts
    lines, textures, binding = [], [], 0
    for typ, name, arr, hints, default in uniforms:
        for h in hints:
            if h in ("hint_screen_texture", "hint_depth_texture", "hint_normal_roughness_texture"):
                raise Unsupported("uniform %s reads the %s" % (name, h[5:].replace("_", " ")))
        if typ in UNSUPPORTED_SAMPLERS:
            raise Unsupported("uniform %s is a %s (2D pictures and arrays of them are carried)" % (name, typ))
        if typ in SAMPLERS:
            if arr:
                raise Unsupported("uniform %s is an array of samplers" % name)
            if binding >= 8:
                raise Unsupported("more than 8 textures")
            kind = SAMPLERS[typ]
            p = params.get(name)
            files = None
            if p and "tex" in p and p["tex"]:
                files = [p["tex"]]
                if kind == 1:
                    raise Unsupported("uniform %s is an array and was given one picture" % name)
            elif p and "layers" in p:
                files = list(p["layers"])
                if kind == 0:
                    files = files[:1]
            elif p and "layers_unread" in p:
                raise Unsupported("uniform %s: its array's layers couldn't be read in the export (%s)" % (name, p["layers_unread"]))
            if files is None:
                notes.append("uniform %s: no texture given, Godot's default for its hints" % name)
            textures.append((binding, tex_flags(kind, hints), files, default_texel(hints)))
            lines.append("layout(set = 3, binding = %d) uniform %s gd_%s;" % (binding, typ, name))
            binding += 1
            continue
        p = params.get(name)
        if arr:
            raise Unsupported("uniform %s is an array" % name)
        if name in globals_:
            p = (rec.get("globals") or {}).get(name)
            notes.append("global uniform %s: the value it had when the scene was exported (%s), held" % (name, p.get("num", p) if p else "none, so 0"))
        if p is not None:
            val = value_glsl(typ, p, hints)
        elif default:
            val = emit_tokens([("x", t) for t in default], tops, {}, "default")
            if "source_color" in hints and re.fullmatch(r"vec[34]\(\s*[-\d.eE+,\s]+\)", val):
                nums = [float(x) for x in val[5:-1].split(",")]
                val = value_glsl(typ, {"color": nums}, hints)
        else:
            val = {"float": "0.0", "int": "0", "uint": "0u", "bool": "false"}.get(typ, "%s(0)" % typ)
        lines.append("const %s gd_%s = %s;" % (typ, name, val))
    # ---- the rest at the top level, renamed
    body = []
    for it in structs + consts:
        body.append(emit_tokens(it, tops, {}, "top"))
    for it in varyings:
        w = [x for x in words(it) if x not in ("varying", "flat", "smooth")]
        body.append(emit_tokens([("x", t) for t in w], tops, {}, "top"))
    for name, it in funcs.items():
        if name in ("vertex", "fragment", "sky"):
            continue
        body.append(emit_tokens(it, tops, ANYWHERE, "func"))
    if shader_type == "sky":
        return sky_hook(rec, lines, body, funcs["sky"], tops), textures, notes, False
    vert = funcs.get("vertex")
    vbody = func_body(vert) if vert else []
    fbody = func_body(funcs["fragment"])
    vmap = dict(ANYWHERE)
    for t, n in V_MEMBERS:
        vmap[n] = "gdV_." + n
    fmap = dict(ANYWHERE)
    for t, n in F_MEMBERS:
        fmap[n] = "gdF_." + n
    vtxt = emit_tokens(vbody, tops, vmap, "vertex") if vbody else ""
    ftxt = emit_tokens(fbody, tops, fmap, "fragment")
    fw = set(words(fbody))
    if vbody and re.search(r"\bgdV_\.VERTEX\s*(?:[+\-*/]?=)(?!=)", vtxt):
        raise Unsupported("its vertex() moves its vertices, which the engine's vertex shader doesn't")
    for n in sorted(fw & NOT_DRAWN):
        notes.append("%s is written and not drawn: the engine has no such light" % n)
    if "ALPHA" in fw and "ALPHA_SCISSOR_THRESHOLD" not in fw:
        notes.append("ALPHA is written: drawn solid (no transparency here)")
    discard = "discard" in fw or "ALPHA_SCISSOR_THRESHOLD" in fw
    need_tb = bool(fw & {"TANGENT", "BINORMAL", "NORMAL_MAP"})
    # ---- the hook
    lift = rec.get("_lift", 0.0)
    g = ["// generated by skate_src/godot/godot_shader.py from %s (%s): don't edit, cook again" % (rec.get("shader", "?"), rec.get("key", "?")),
         "const float GD_UPM = %s, GD_LIFT = %s;   // the engine's units a metre, and how far godot2set.py lifted the scene" % (fnum(UPM), fnum(lift)),
         "const float GD_PI = 3.14159265358979, GD_TAU = 6.28318530717959, GD_E = 2.71828182845905;",
         "vec3 gdEye() { return V.eye.xyz / GD_UPM - vec3(0.0, GD_LIFT, 0.0); }"]
    g += lines
    g.append("struct GdV { %s };" % " ".join("%s %s;" % m for m in V_MEMBERS))
    g.append("struct GdF { %s };" % " ".join("%s %s;" % m for m in F_MEMBERS))
    g += body
    g.append("void gd_vertex(inout GdV gdV_) {%s}" % vtxt)
    g.append("void gd_fragment(inout GdF gdF_) {%s}" % ftxt)
    g.append("""GdSurf gdSurface(vec2 uvIn, vec3 nWorld) {
    vec3 gw = vW / GD_UPM - vec3(0.0, GD_LIFT, 0.0);   // this pixel in the scene's own metres
    GdV v; v.VERTEX = gw; v.NORMAL = nWorld; v.TANGENT = vec3(1.0, 0.0, 0.0); v.BINORMAL = vec3(0.0, 0.0, 1.0); v.UV = uvIn; v.UV2 = uvIn; v.COLOR = vec4(1.0);
    v.POINT_SIZE = 1.0; v.ROUGHNESS = 1.0; v.CUSTOM0 = v.CUSTOM1 = v.CUSTOM2 = v.CUSTOM3 = v.INSTANCE_CUSTOM = vec4(0.0);
    gd_vertex(v);
    vec3 fw = normalize(V.fwd.xyz), rt = cross(fw, vec3(0.0, 1.0, 0.0)); float rl = length(rt); rt = rl > 1e-4 ? rt / rl : vec3(1.0, 0.0, 0.0);
    mat3 toWorld = mat3(rt, cross(rt, fw), -fw), toView = transpose(toWorld);   // the lens's own axes, taken level
    GdF f; f.VERTEX = toView * (gw - gdEye()); f.LIGHT_VERTEX = f.VERTEX; f.NORMAL = toView * nWorld; f.VIEW = normalize(-f.VERTEX); f.UV = v.UV; f.UV2 = v.UV2; f.COLOR = v.COLOR;
    f.SCREEN_UV = (gl_FragCoord.xy - V.cl2.zw) / V.cl3.xy;""")
    if need_tb:
        g.append("""    { vec3 dp1 = dFdx(f.VERTEX), dp2 = dFdy(f.VERTEX); vec2 du1 = dFdx(f.UV), du2 = dFdy(f.UV); vec3 dp2p = cross(dp2, f.NORMAL), dp1p = cross(f.NORMAL, dp1);
      vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y; float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12)); f.TANGENT = T * im; f.BINORMAL = B * im; }""")
    else:
        g.append("    f.TANGENT = toView * vec3(1.0, 0.0, 0.0); f.BINORMAL = toView * vec3(0.0, 0.0, 1.0);")
    g.append("""    f.ALBEDO = vec3(1.0); f.ALPHA = 1.0; f.ALPHA_SCISSOR_THRESHOLD = 0.0; f.ALPHA_HASH_SCALE = 1.0; f.ALPHA_ANTIALIASING_EDGE = 0.0; f.ALPHA_TEXTURE_COORDINATE = vec2(0.0);
    f.PREMUL_ALPHA_FACTOR = 1.0; f.METALLIC = 0.0; f.SPECULAR = 0.5; f.ROUGHNESS = 1.0; f.RIM = 0.0; f.RIM_TINT = 0.5; f.CLEARCOAT = 0.0; f.CLEARCOAT_ROUGHNESS = 0.0;
    f.ANISOTROPY = 0.0; f.ANISOTROPY_FLOW = vec2(1.0, 0.0); f.SSS_STRENGTH = 0.0; f.SSS_TRANSMITTANCE_COLOR = vec4(0.0); f.SSS_TRANSMITTANCE_DEPTH = 0.1;
    f.SSS_TRANSMITTANCE_BOOST = 0.0; f.BACKLIGHT = vec3(0.0); f.AO = 1.0; f.AO_LIGHT_AFFECT = 0.0; f.EMISSION = vec3(0.0); f.FOG = vec4(0.0); f.RADIANCE = vec4(0.0);
    f.IRRADIANCE = vec4(0.0); f.NORMAL_MAP = vec3(0.5, 0.5, 1.0); f.NORMAL_MAP_DEPTH = 1.0; f.BENT_NORMAL_MAP = vec3(0.5, 0.5, 1.0);
    gd_fragment(f);""")
    if "NORMAL_MAP" in fw:
        g.append("""    { vec3 nm = f.NORMAL_MAP; nm.xy = nm.xy * 2.0 - 1.0; nm.z = sqrt(max(0.0, 1.0 - dot(nm.xy, nm.xy)));
      f.NORMAL = normalize(mix(f.NORMAL, f.TANGENT * nm.x + f.BINORMAL * nm.y + f.NORMAL * nm.z, f.NORMAL_MAP_DEPTH)); }""")
    if "ALPHA_SCISSOR_THRESHOLD" in fw:
        g.append("    if (f.ALPHA < f.ALPHA_SCISSOR_THRESHOLD) discard;")
    if "unshaded" in render:   # its own colour, lit by nothing: given off, with nothing for the light to fall on and no highlight
        g.append("""    GdSurf s; s.albedo = vec3(0.0); s.normal = nWorld; s.rough = 1.0; s.metal = 0.0; s.spec = 0.0; s.ao = 1.0; s.emit = f.ALBEDO + f.EMISSION;
    return s;
}""")
    else:
        g.append("""    GdSurf s; s.albedo = f.ALBEDO; s.normal = toWorld * f.NORMAL; s.rough = f.ROUGHNESS; s.metal = f.METALLIC; s.spec = f.SPECULAR; s.ao = f.AO; s.emit = f.EMISSION;
    return s;
}""")
    return "\n".join(g) + "\n", textures, notes, discard


# a sky shader's built-ins, as the engine's sky dome (shaders/skydome.frag) has them. EYEDIR is the world's way out from the eye, SKY_COORDS the
# panorama's coordinates worked out as Godot's sky.glsl does, the first light the engine's sun, the others none
SKY_BUILTINS = {"EYEDIR": "gdDir", "SKY_COORDS": "gdSkyUV", "POSITION": "gdEye()", "SCREEN_UV": "vUV", "AT_CUBEMAP_PASS": "false",
                "AT_HALF_RES_PASS": "false", "AT_QUARTER_RES_PASS": "false", "LIGHT0_ENABLED": "(V.sunDir.w > 0.0)", "LIGHT0_DIRECTION": "normalize(V.sunDir.xyz)",
                "LIGHT0_ENERGY": "V.sunDir.w", "LIGHT0_COLOR": "V.sunCol.rgb", "LIGHT0_SIZE": "0.0093", "COLOR": "gdS_.COLOR", "ALPHA": "gdS_.ALPHA", "FOG": "gdS_.FOG"}
for _k in (1, 2, 3):
    SKY_BUILTINS.update({"LIGHT%d_ENABLED" % _k: "false", "LIGHT%d_DIRECTION" % _k: "vec3(0.0, 1.0, 0.0)", "LIGHT%d_ENERGY" % _k: "0.0",
                         "LIGHT%d_COLOR" % _k: "vec3(0.0)", "LIGHT%d_SIZE" % _k: "0.0"})
SKY_NEVER = {"RADIANCE", "HALF_RES_COLOR", "QUARTER_RES_COLOR"}


def sky_hook(rec, lines, body, fn, tops):
    sbody = func_body(fn)
    used = set(words(sbody))
    for n in sorted(used & SKY_NEVER):
        raise Unsupported("its sky() reads %s" % n)
    smap = dict(ANYWHERE)
    smap.update(SKY_BUILTINS)
    stxt = emit_tokens(sbody, tops, smap, "sky")
    g = ["// generated by skate_src/godot/godot_shader.py from %s (%s), a sky: don't edit, cook again" % (rec.get("shader", "?"), rec.get("key", "?")),
         "const float GD_UPM = %s, GD_LIFT = %s;" % (fnum(UPM), fnum(rec.get("_lift", 0.0))),
         "const float GD_PI = 3.14159265358979, GD_TAU = 6.28318530717959, GD_E = 2.71828182845905;",
         "vec3 gdEye() { return V.eye.xyz / GD_UPM - vec3(0.0, GD_LIFT, 0.0); }",
         "vec3 gdDir; vec2 gdSkyUV;   // the way out from the eye, and where that is on a panorama"]
    g += lines
    g.append("struct GdS { vec3 COLOR; float ALPHA; vec4 FOG; };")
    g += body
    g.append("void gd_sky(inout GdS gdS_) {%s}" % stxt)
    g.append("""vec3 gdSky(vec3 dirIn) {
    gdDir = dirIn; float a = atan(dirIn.x, -dirIn.z); if (a < 0.0) a += 6.28318530718;
    gdSkyUV = vec2(a / 6.28318530718, acos(clamp(dirIn.y, -1.0, 1.0)) / 3.14159265359);
    GdS s; s.COLOR = vec3(0.0); s.ALPHA = 1.0; s.FOG = vec4(0.0);
    gd_sky(s);
    return s.COLOR;
}""")
    return "\n".join(g) + "\n"


def func_body(it):
    """the tokens between a function's outer braces"""
    depth, start = 0, None
    for i, t in enumerate(it):
        if t[1] == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif t[1] == "}":
            depth -= 1
            if depth == 0:
                return it[start:i]
    return []


def emit_tokens(toks, tops, builtins, where):
    """tokens back to text: their top-level names as gd_<name>, a stage's built-ins as the engine has them, a GLSL keyword as <name>_.
    Tokens of kind 'x' came with no whitespace: a space goes between two words"""
    out, prev = [], None
    for k, t in toks:
        if k == "ws":
            out.append(t)
            continue
        t2 = t
        if re.fullmatch(r"[A-Za-z_]\w*", t) and prev != ".":
            if t in NEVER:
                raise Unsupported("it reads %s" % t)
            if t in tops:
                t2 = "gd_" + t
            elif t in builtins:
                t2 = builtins[t]
            elif t in GLSL_RESERVED:
                t2 = t + "_"
        if k == "x" and out and re.match(r"\w", t2[:1]) and re.search(r"\w$", out[-1]):
            out.append(" ")
        out.append(t2)
        prev = t
    return "".join(out)


def mesh_frag_abi(path=MESH_FRAG):
    m = re.search(r"#define GODOT_HOOK_ABI (\d+)", open(path, encoding="utf-8").read())
    if not m:
        raise SystemExit("mesh.frag has no GODOT_HOOK_ABI: the engine's shader isn't the one this cook knows")
    return int(m.group(1))


def glslang():
    for p in (shutil.which("glslangValidator"), r"C:\msys64\mingw64\bin\glslangValidator.exe", r"C:\VulkanSDK\1.4.363.0\Bin\glslangValidator.exe"):
        if p and os.path.exists(p):
            return p
    raise SystemExit("no glslangValidator (MSYS2's mingw-w64-x86_64-glslang, or the Vulkan SDK's)")


def cook(rec, export_dir, out_dir, lift):
    """one shader material into <out_dir>/<key>.gshd, .spv, .glsl and its pictures. Returns (gshd path, notes); raises Unsupported"""
    from PIL import Image   # the pictures: read and written again as plain RGBA, so the engine's reader takes them
    rec = dict(rec, _lift=lift)
    out_dir, export_dir = os.path.abspath(out_dir), os.path.abspath(export_dir)   # the compiler runs in the engine's shaders folder
    glsl, textures, notes, discard = translate(rec, export_dir)
    key = rec["key"]
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, key + ".glsl"), "w", encoding="utf-8", newline="\n").write(glsl)
    tool = glslang()
    with tempfile.TemporaryDirectory() as tmp:
        open(os.path.join(tmp, "godot_hook.glsl"), "w", encoding="utf-8", newline="\n").write(glsl)
        spv = os.path.join(out_dir, key + ".spv")
        sky = re.search(r"\bshader_type\s+sky\b", rec["code"]) is not None
        src = SKY_FRAG if sky else MESH_FRAG
        cmd = [tool, "-V"] + (["-DGODOT_SKY_HOOK=1"] if sky else ["-DGODOT_HOOK=1"] + ([] if discard else ["-DEARLY_Z=1"])) + ["-I" + tmp, "-o", spv, src]
        env = dict(os.environ, PATH=r"C:\msys64\mingw64\bin;" + os.environ["PATH"])
        r = subprocess.run(cmd, cwd=os.path.dirname(MESH_FRAG), env=env, capture_output=True, text=True)
        if r.returncode:
            raise Unsupported("its translation doesn't compile: %s" % " | ".join(l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l)[:900])
    lines = ["gshd 1", "# %s from %s, by skate_src/godot/godot_shader.py: cook again, don't edit" % (key, rec.get("shader", "?")),
             "abi %d" % mesh_frag_abi(src), "kind %s" % ("sky" if sky else "surface"), "spv %s" % (key + ".spv")]
    for b, flags, files, texel in textures:
        names = []
        if files:
            for i, fn in enumerate(files):
                src = os.path.join(export_dir, "shaders", fn)
                dst = "%s_t%d_%d.png" % (key, b, i)
                Image.open(src).convert("RGBA").save(os.path.join(out_dir, dst))
                names.append(dst)
        else:
            dst = "%s_t%d_default.png" % (key, b)
            Image.new("RGBA", (1, 1), texel).save(os.path.join(out_dir, dst))
            names.append(dst)
        lines.append("tex %d %d %d %s" % (b, flags, len(names), " ".join(names)))
    path = os.path.join(out_dir, key + ".gshd")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    return path, notes


def main():
    ap = __import__("argparse").ArgumentParser(description=__doc__)
    ap.add_argument("export")
    ap.add_argument("name")
    ap.add_argument("out")
    ap.add_argument("--lift", type=float, default=0.0)
    a = ap.parse_args()
    man = json.load(open(os.path.join(a.export, a.name + ".json"), encoding="utf-8"))
    for rec in man.get("shaders", []):
        try:
            p, notes = cook(rec, a.export, a.out, a.lift)
            print("%s (%s): %s%s" % (rec["key"], rec.get("shader"), p, "".join("\n   " + n for n in notes)))
        except Unsupported as e:
            print("%s (%s): NOT MADE, %s" % (rec["key"], rec.get("shader"), e))
    return 0


if __name__ == "__main__":
    sys.exit(main())
