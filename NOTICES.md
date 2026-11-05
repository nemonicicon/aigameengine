# Notices

What the licence covers, whose work is inside a build besides ours, and what must never be in the public kit. Checked against the tree on 2026-11-05 (queue step 28): the build lines, the engine's `#include`s and the DLLs a Windows build asks for.

## What the MIT licence covers

`LICENSE` is the engine's and its tools': everything `kit/manifest.txt` sends to the public kit (aigameengine). jacob, 11-04, asked which licence: *"mit."*

In the private workbench (reva) there is more than that, and the licence doesn't reach it: the shows, their scripts and stories, the characters, the music and voices, and every bought or licensed asset. None of it is in the kit, and the manifest is what keeps it out.

## Compiled into the engine

These are in every build's binary.

| What | Version | From | Its terms |
|---|---|---|---|
| stb_image | 2.30 | the system's stb package (`<stb/stb_image.h>`) | public domain (Unlicense), or MIT at your choice |
| stb_truetype | 1.26 | the same (`<stb/stb_truetype.h>`): a font a run names for captions | the same |
| stb_easy_font | 1.1 | the same (`<stb/stb_easy_font.h>`): the debug overlay's letters | the same |
| stb_vorbis | 1.22 | in the tree, `reva_native/third_party/stb_vorbis.c` | the same |

All four are Sean Barrett's. Under the MIT choice their notice is:

> Copyright (c) 2017 Sean Barrett
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Nothing of raylib's or Wicked Engine's code is in the tree. The plan names both as engines to learn from, and that's all they were.

## Linked, not copied

| What | Where | Its terms | Shipped with a packaged game? |
|---|---|---|---|
| SDL2 | every build (window, pads, sound) | zlib licence, Sam Lantinga | Windows: `SDL2.dll`, with its licence text. Linux and the Pi: the system's |
| The Vulkan loader | the Vulkan build (`vulkan-1.dll`, `libvulkan.so`) | Apache 2.0, The Khronos Group | No: it comes with the machine's graphics driver. The build on the engine's own rasterizer doesn't link it |
| The Vulkan headers | at build time only | Apache 2.0 or MIT, The Khronos Group | No |
| libdrm | the Pi's direct road | MIT | No: the system's |
| mingw-w64's runtime and winpthreads | Windows builds (`libwinpthread-1.dll`, and the runtime inside the exe) | ZPL 2.1 and public domain; winpthreads MIT and BSD | Yes, with their licence texts |
| GCC's runtime (libgcc) | inside every build | GPL 3 with the GCC Runtime Library Exception, which allows this | In the exe |
| The C library | Linux and the Pi (glibc, linked at run time) | LGPL 2.1 | No: the system's |

`reva_native/package.py` copies the licence texts of what it ships out of the toolchain's own folders into the package's `licenses/`, so a package always carries the texts for the versions it was built with.

## In the tree, and somebody else's

| What | Its terms |
|---|---|
| `reva_native/clubcloth/godot/gdextension_interface.h` | MIT. Copyright (c) 2014-present Godot Engine contributors; Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur. The file carries its notice |
| `reva_native/third_party/stb_vorbis.c` | as above. The file carries its notice |

## Made with, and not shipped

- **The setup prompts** in `reva_native/voice/` were spoken by Kokoro (Apache 2.0) on our own machine and are kept as wavs (`make_voice.py`). No speech engine is in a build.
- **The typeface** is the TV font (`reva_native/engine/tvfont.inc`, `tvfont2.inc`), drawn for this engine. It is the only typeface that ships. A run may name a font of its own for captions (`REVA_CAPFONT`), which is then that run's maker's to have the right to.
- **Wide glyphs** for Chinese, Japanese and Korean (`data/<name>.tvw`) are baked from a font the game's maker supplies, and that font's licence goes with the file. The engine ships none, and git ignores them.
- **The tools** run on Python with Pillow and numpy, and some on Blender, ffmpeg and glslang. None of those is distributed with the engine.
- **The sample game, the talk game, the tower and the rest of `games/`** make their bodies, sets and sounds by code (`make_body.py`, `make_assets.py`), so they carry nobody else's content.

## What must never be in the kit or a public repository

- Bought characters and anything made from them. Anima (Fab, Standard License) may be changed and shipped inside our own games and films, and may not be put in a public repository, the open kit, or an editor that exports. `design/character_standard.md` has it by section.
- A glyph file baked from a font that came with an operating system.
- Motion that came with a third-party pack. It's reference only.
- The shows' songs, lyrics and recordings.

`kit/manifest.txt` names every path that leaves the workbench, and `python kit/kit_sync.py` reports anything new as undecided until a line names it.
