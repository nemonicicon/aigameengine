# The pack format

*A pack (`.rpack`) is one file holding a body or a prop: its skeleton, meshes, materials, clips, textures and morphs. This is what's on disk today, written from the loader (`engine/load.inc`) and the writers (`skate_src/rpackwrite.py`, `skate_src/author/rpack.py`).*

`anim_bench/packdump.py` holds this document to the truth. `--check` tests every rule marked **checked** below, and on 11-02 all 404 packs on disk passed. `--selftest` breaks a real pack 28 ways to prove the checks can fail, and none got through. `packcheck.py` in `skate_src/author` tests what a body means (the bind, the hands, the weights), which this document doesn't cover.

**The engine holds a pack to these rules too** (queue step 28, 11-05, `engine/packcheck.inc`). Until then only the Python check did, and the loader followed every offset, count and index on trust: a fuzzer made it read and write outside the file a dozen ways with one changed byte. `packCheck` now reads the whole file once before the loader touches it, and again after its chunks are merged: the header, every table against the file's length, every name, parent, bone, joint, index, clip, track, texture and morph. A pack that fails is refused by name, with the reason, and nothing of it is used. So a rule added here needs its line in three places: this document, `packdump.py --check`, and `packCheck`.

## The file

Everything is little-endian. Offsets count from the start of the file. Nothing promises alignment: 191 of the 404 packs have arrays that don't start on a 4-byte boundary, so a strict reader has to copy, not cast.

A pack opens with the magic `RPK1` and 15 unsigned 32-bit words:

| Word | Name | Meaning |
|---|---|---|
| 0 | `ver` | 2 (**checked**) |
| 1 | `nNodes` | |
| 2 | `nMeshes` | |
| 3 | `nMats` | |
| 4 | `nClips` | |
| 5 | `nTex` | |
| 6 | `offNodes` | |
| 7 | `offMeshes` | |
| 8 | `offMats` | |
| 9 | `offClips` | |
| 10 | `offTexTable` | |
| 11 | `offStr` | |
| 12 | `offSize` | |
| 13 | `total` | The file's size (**checked**) |
| 14 | `offMorphs` | 0 for none. ⛔ Any writer has to keep this word: `makepajamas.py` once wrote 0 here and lost the morphs |

Every table and array has to fit inside the file (**checked**). The order of the parts in between is free, and the tools rely on that: an authoring tool appends new data at the end and repoints the header, so nothing already written moves.

## Strings

All names are indices into one table: a `u32` count, then count pairs of (`u32` offset, `u32` length), then the characters. Offsets count from the end of the pairs. There are no terminators. Every name's index is in range (**checked**).

## Nodes

52 bytes each, `<IiI3f4f3f`:

| Field | Meaning |
|---|---|
| `name` | String index |
| `parent` | Node index, or -1. **A parent comes before its children** (**checked**): the loader walks the table once |
| `kind` | 0 plain, 1 bone, 2 a mesh's node (**checked**) |
| `t[3]`, `q[4]`, `s[3]` | Rest translation, rotation (x, y, z, w) and scale |

## Meshes

60 bytes each, 15 `u32`: `name, node, verts, idxCount, boneCount, flags, offPos, offNorm, offUV, offJoints, offWeights, offIdx, offBoneMap, offIBM, offBind`.

`node` is in range and `idxCount` is whole triangles (**checked**). `flags` bits: 1 normals, 2 UVs, 4 drawable, 8 goes in the alpha-blend pass. No other bit is set anywhere (**checked**).

| Array | Size | Notes |
|---|---|---|
| positions | `verts` x 3 floats | |
| normals | `verts` x 3 floats | With flag 1 |
| UVs | `verts` x 2 floats | With flag 2. They can run outside 0..1, and the sampler wraps |
| joints | `verts` x 4 `u8` | Index the bone map (**checked**) |
| weights | `verts` x 4 `u8` | Sum to 255 |
| indices | `idxCount` x `u32` | All below `verts` (**checked**) |
| bone map | `boneCount` x `u16` | Node indices (**checked**). At most 256 |
| inverse binds | `boneCount` x 16 floats | Column-major |
| bind | 16 floats | With flag 4 |

## Materials

36 bytes each, and most of it unused. `u32 mesh` at 0 (**checked**), `u32 flags` at 16 (1 nearest filter, 2 flip V, 4 its texture's alpha cuts it out, 8 set by older writers on 112 of the packs here (the crowd's merged meshes, the Character_* cast) and read by nothing, 16 its texture's alpha from 128 up is how much of the room each point sees, 128 none of it and 255 all, which the engine takes off the room's light and not the colour: `GFX_OCCLUSION`, written by `skate_src/reva/mouth_ao.py` for the inside of a mouth), `i32 tex` at 24 (-1 for none), and `i32` at 28, the normal map's texture slot plus 1 (0 for none, so old packs read as none). Both texture slots are in range (**checked**). A mesh takes the first material row naming it.

## Clips

20 bytes each, `<IfIII`: `name, fps, frames, nTracks, offTracks`. `fps` is above zero (**checked**).

A track is 12 bytes, `<III`: `node, type, offData`, with `frames` values at `offData`:

| Type | Moves | Floats per frame |
|---|---|---|
| 0 | Rotation, x y z w | 4 |
| 1 | Translation | 3 |
| 2 | Scale | 3 |
| 3 | A morph's weight; `node` is then the morph's slot | 1 |

The type is one of these and, for types 0 to 2, the node is in range (**checked**).

## Textures

16 bytes each: the first 8 unused, `u32 off` at 8, `u32 len` at 12. A blob is an image file that `stb_image` decodes at load, which today means PNG or JPEG (**checked**, by the file's own magic). Length 0 means no image.

## The size block

12 floats at `offSize`, of which the loader reads three: 7 the sole's height (NaN for unknown), 9 the mesh's measured height (NaN for unknown), and 10, where 1.0 says those were measured on the bench and can be trusted exactly.

## Morphs

At `offMorphs`: a `u32` count, then 16 bytes each: `mesh, name, sparseCount, off`. At `off`, `sparseCount` entries of 16 bytes: `u32 vertex`, then the delta as 3 floats. The mesh and every vertex are in range (**checked**). The engine takes at most 256 (MORPH_MAX, 128 until 11-11: REVA's face with the ARKit set carried onto it is 139) and says so if a pack has more.

## Names that mean something

The format has no fields for these. The engine reads them out of names at load, so a name is part of the format. All of it is case-insensitive unless it's shown capitalized.

**Nodes that are switches.** Their position doesn't matter, only that they exist: `ToonStyle`, `ToonSet`, `Glow`, `NoCast`, `Open`, `MirrorOnly`, `Floor`, `TrueScale`, `VertexCollide`, and `Alpha<NN>`, a sheer prop at NN percent. What each does is in `engine_capabilities.md`.

**Nodes that are marks.** `face_jaw`, `face_mouth`, `face_mouth_l`, `face_mouth_r`, `face_eye_l`, `face_eye_r`, `face_chin` are measured face positions, usually with nothing skinned to them.

**Bones found by name.**
- The canon, 16 body bones and 30 finger joints, across UE, Mixamo, CC and Daz naming: `canonOf` in `engine/pack.inc`. It's how a clip moves from one rig to another.
- Hands: a bone with `hand` in its name, the side taken from a trailing `_l`/`_r` or `left`/`right`.
- Chains that swing: `tail`, `pony`, `braid`, `cloth`, `hair`, `bang`, `tress`, `ribbon`, and `ear` with guards against `arm`, `rear`, `spear`, `beard`, `heart` and `gear`. At most 32 chains of 14 bones.
- Jiggle: `breast`. Stretch: `Stretch_<x>` with its `Anchor_<x>`. Contacts on a board: `wheel`.

**Mesh names.** `#rrggbb` anywhere in the name is the colour of an untextured mesh. `~worn`, `~cloth` and `~skirt~` are garments, and `~face~` is a drawn face. `wear~<slot>~<piece>~<outfit>+<outfit>[~h=a,b]` is a wardrobe piece, and `skin~h=a,b` is skin those pieces cover. `mouth_interior` is the inside of a mouth.

**Clip names.** ⛔ A name changes behaviour, so a clip's name is checked by `rpack.py lint`.
- Roles a pack elects for itself: walk (`w_` prefix, or contains `alk`); run (`run`, `sprint`, `flee`); attack (`attack`, `punch`, `kick`, `slash`, `swing`, `strike`, `jab`); down (`death`, `dying`, `dead`, `fall`, `down`, `defeat`); idle (`idle`, `stand`, only if the clip loops and isn't a floor pose).
- Exact names a donor bank elects: `w_walk`, `w_run`, `w_fly`, `w_ski`, `idle`, `x_gunfire`, `x_down`, and the prefixes `v_walk`, `x_fidget`, `x_convo`.
- `m_` is the dance vocabulary. `z_` is the authored namespace, safe from all of the above.

**Files found by name.** `v1_moves.rpack` in the packs folder, `packs/v1_zmoves.rpack`, and `v1_mob1.rpack` and `v1_kaeli.rpack` in the working folder are the donor banks. Beside a pack, `<pack>.gait` holds each clip's authored speed. `credits.txt` holds `label|artist` lines.

⛔ **What shouldn't be here.** The loader still gives a few specific labels their own treatment: a label naming a fish or bird flies, `starfish` keeps its size, `hina` is scaled up 2.3 times. That's content in the engine, left over from the club, and it goes when someone works on the loader.

## Writing a pack

New importers use `write_pack()` in `skate_src/rpackwrite.py`, the one shared writer. `glb2rpack.py` and `glb2rpack_anim.py` carry their own older copies. `games/sample/make_body.py` is the smallest whole example, and its output, `games/sample/packs/v1_sample.rpack`, is the golden pack: `make_body.py --check` says the code still builds it byte for byte, and the sample's receipt says the engine still reads it the same way. A tool that adds to a finished pack appends and repoints, as `rpack.py append_clips` does, and never rewrites what's there. It writes through `save_pack`, which keeps the file's chunks. A bake starts over, and it keeps what was authored on the pack it replaces: see `CLIP`, `MRPH` and `FACE` below. `--fresh` really does start over.

## RPK2: chunks after the old body

The rule for growing the format is that old bytes never move. New data goes after the old body, and a reader of the old format still reads everything it knew, because it follows offsets and stops at `total`. RPK2 keeps the magic `RPK1` and adds this after `total`, all of it **checked**:

| Part | Layout |
|---|---|
| Chunks | Each on a 16-byte line |
| Directory | One 16-byte entry a chunk: `char tag[4]`, `u32 off`, `u32 size`, `u32 crc32` |
| Footer | The last 16 bytes of the file: `u32 dirOff`, `u32 dirCount`, `u32 fileSize`, then `RPK2` |

A reader looks at the last 4 bytes, and a footer whose `fileSize` isn't the file's size is a broken pack. An unknown tag is skipped, so a new chunk doesn't break an old engine.

**`TEXC`, compressed textures.** A `u32` count, then 32 bytes each: `u32 slot, format, w, h, levels, flags, off, size`.
- `slot` is the texture's place in the old table, whose entry then has length 0.
- `format` is 1 for BC1 or 3 for BC3. They're the two the Pi and the PC both sample. The cook uses BC3 only for a texture with alpha. 7 is BC7, from `cook_game.py --bc --bc7`: 16 bytes a block, a better picture, and only the PC can sample it, so a game cooks once for each. The cook writes only BC7's mode 6.
- `flags` bit 0 says the texture has holes (alpha under 128), so a draw needs the cutout pipeline, which costs this GPU its early depth test.
- `off` is from the start of the file, on a 16-byte line, and `size` is every level together, largest first down to 1x1, with no gaps. A level is `((w+3)/4) * ((h+3)/4)` blocks of 8 bytes (BC1) or 16 (BC3).
- A material's flip is already baked in.

The engine hands `TEXC` straight to the GPU on Vulkan. The GL path and any GPU without BC decode the top level to plain pixels at load (`bcDecode`). The no-display build skips it.

**`MATL`, what each mesh is made of (queue step 22).** A `u32` count, then 64 bytes each: a 40-byte mesh name, zero-padded, and six `f32`: how rough (0 a mirror to 1 chalk), how much a metal (0 or 1), how much fine detail up close (0 to 1), how deep its surface is for parallax (0 to 0.25, in the texture's widths; the height is its normal map's alpha), its kind for a body (step 23: 0 plain, 1 skin, 2 hair, 3 an eye, 4 cloth, 5 its texture's alpha says pixel by pixel, 6 fur), and which mesh of that name it means, counted from 1 in the pack's order, 0 for every one (a body whose parts all carry one name needs it: `material.py "<mesh>@N=..."`). For fur the fourth number is how long it is in metres, the third how fine, and the second how curly, 0 straight to 1 wool. By name, so a mesh can move. `skate_src/author/material.py` writes it. A bake carries it across as it is (`rpackchunks.carry`): a record whose mesh has gone finds nothing. A mesh the chunk doesn't name is looked up by words in its name: `data/materials.txt` (`<word> <rough> <metal> [detail]`), then the engine's own short list; nothing matching is rough 0.85 and not a metal. The engine clamps what it reads, and a chunk whose count doesn't fit its size is ignored, loudly.

**`NOTE`.** Text saying what made the pack.

`cook_game.py --bc` writes RPK2. Its packs also keep every array on a 4-byte line, so a strict reader can cast them, which is what UBSan wanted. `anim_bench/golden/rpk2/v1_sample.rpack` is the golden copy, the sample's body run through `cook_game.py --retex 1024 --bc`. The sample's receipt plays it and must print the same lines as the plain one.

**`CLIP`, `MRPH`, `FACE`: what's authored on a body.** A bake starts over, and this is what it can't lose. `skate_src/rpackchunks.py` is the code, and `engine/packchunks.inc` is the same thing in C. When `glb2rpack_anim.py` or `write_pack` bakes over a pack that's already there, whatever the old pack had and the new one hasn't goes in these chunks after the new body. At load, the engine and `Rpack` add to the body what it hasn't got, by appending and repointing in memory, and a name in the body beats a name in a chunk. So nothing else in either has to know. `REVA_PACKSUM=1` and `rpackchunks.py sum` print the same hash of what a pack means once it's merged.

They're named, not numbered, so a bone can move. Every offset counts from the chunk's start, everything sits on a 4-byte line, and a name is a `u32` length and its characters. All of it is **checked**.

| Chunk | A `u32` count, then | At the offsets |
|---|---|---|
| `CLIP` | 20 bytes each: `nameOff`, `f32 fps`, `frames`, `nTracks`, `tracksOff` | Tracks, 12 bytes each: `nameOff`, `type`, `dataOff`, with `frames` values at `dataOff` as in the old format |
| `MRPH` | 20 bytes each: `nameOff`, `meshOff`, `meshVerts`, `nSparse`, `dataOff` | `nSparse` entries as in the old format. The mesh's size has to match, or the morph's left out, out loud |
| `FACE` | 52 bytes each: `nameOff`, `parentOff` (0xffffffff for none), `kind`, then `t[3] q[4] s[3]` | Marks: nodes nothing's skinned to. A parent comes first |

Names aren't unique, because a rig can have two nodes of a name, and a morph's name can be on several meshes or twice on one. So a reference says which it means. A node is `name`, or `name<tab>1` for the second of that name. A mesh is `name`, or `name<ctrl-A>1`. A morph in a track is `mesh<tab>name<tab>which`, with `which` counting from 0.

**What a bake keeps.**
- Clips, by name.
- Morphs, by mesh and name. If the mesh changed, a morph is kept only if every vertex it moves is still in the same place.
- Nodes nothing's skinned to, except the switches (`ToonStyle` and the rest), which are the glb's to decide.

What it can't keep it lists out loud, and it copies the old pack to `<pack>.before_bake`: a bone a mesh is skinned to, and a mesh the new bake hasn't got, such as a garment.

**`TOOL`.** A jaw's weights and the inside of a mouth are in the mesh, so they can't be carried, but the tools that made them can run again. Text, one line a tool: its name and its arguments, tab-separated. `jaw.py`, `mouth_interior.py` and `fingers.py` write their line when they run, and a bake runs them again in that order.

**Tools on a finished pack.** `Rpack` reads a pack merged. `save_pack` writes a tool's new body with the chunks it had, taking out what's now in the body and what the tool dropped. `append_clips` on a pack with a footer adds to `CLIP` and leaves the body and the compressed textures exactly as they are.

`anim_bench/golden/chunks/v1_sample.rpack` is the sample's body with its clips in a chunk, and the sample's receipt plays it and must print the same lines as the plain one. `rpackchunks.py tochunks` makes any pack that way, which is the test of the reader: on 11-03 all 163 packs on disk meant the same after it.
