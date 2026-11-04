# aigameengine: the plan (2026-11-02)

*Written after a day of questions to jacob, measurements on the real Pi and tube, and seven tracks of research. Updated 11-03 with jacob's answers. This doc is the queue, and jacob's newest word reorders it. Evidence: [the research report](../reports/Firestrider%20low%20level%20engine%20research.md) (citations, conflicts, what is unmeasured) and [today's measurements](../research_notes/Firestrider%20low%20level%20engine%20research/pi_measurements.md).*

> jacob, 09-30: *"a cross platform ai first game engine that can be run on a raspberry pi 4 as the dev console and the target console, taking great inspiration from n64 recomp techniques, to produce games that run flawlessly and scale elegantly from the weakest hardware to the most powerful gaming computer."*
>
> jacob, 11-02: *"this is firestriders game engine, that hopefully we can use well into the future."*

## Where it stands, in one page

Phases 0 and 1 are closed. Phases 2 to 6 are built through the queue's step 16 (11-04): 2D, 3D action, crowds, split screen, a streaming world, objects and physics, menus, faces and conversations, people built while a game runs, level changes and paths, each proven by one small game, with the gates' Pi replays waiting for performance's turn. Phase 7 has its first three steps and the toon pass built, and step 18, film and the old game modes onto Vulkan, is most of the way: film's test take renders on Vulkan beside its reference, and the lighthouse door, the build lines and taking the old renderer out are what's left. Phase 8 hasn't started. [The queue](#the-queue) is what's next, in order, and [the review of 11-04](#review-11-04) says where it all stands, what's uncommitted, the gaps found on the way, and what's put to jacob.

jacob, 11-03: *"the engine. gaps in the game engine. the engine. the games are secondary to building the engine."*

On 11-02, the first day the engine ran on the Pi, the proof game, Reva vs Zombies, ran at 4.4 fps.

The Pi itself is not the problem. In a bare test its GPU drew **400 individually posed bodies of 800 triangles in 13.2 ms of a 16.68 ms field**. The zombie game was sending 9.5 million indices a frame, about 12 times what fits, and **one film-quality hero body (1.06 million triangles) was about 95% of that**. Swapping her for a street citizen took the game from 4.4 to about 30 fps in one boot.

The engine's gaps are the work now. jacob, 11-02: *"we are not trying to do preformance on the pi right now, we are trying to flesh out the engine."* And 11-03: *"really focused more on capabilities and features right now than optimizing for the pi, that will come later on."* The order is: fix the tick contract, build one body path and the Vulkan renderer through the zombie game, then cut the engine away from its games around that working proof. After that come any TV, the Pi dev console, the other kinds of game, and going public, with the PC's graphics list running alongside them.

## What jacob decided

These are requirements, not options. Each came from jacob on 11-02 unless dated.

| Question | Decision |
|---|---|
| The two repos | `reva` is the workbench: game modes and film output stay there. `aigameengine` is the generalized engine, agnostic to any game, and it carries the animation and film tooling but not the output. Code is built in reva and synced out. |
| Who it's for | Public someday, under the name aigameengine |
| The proof | Reva vs Zombies, 100 bodies on screen, on the tube, no dropped frames |
| Kinds of game | 3D character action, big crowds and RTS, 2D and arcade, big open worlds |
| Multiplayer | Couch co-op and split screen up to 4 players on one TV, online someday |
| The hard rule | *"any frame rate drop at all is unacceptable. its 2026. 480i gaming should never dip below native frame rate."* 60 new images a second at 480i and 240p |
| Video mode | Each game picks 240p or 480i, *"maybe even switching between as needed"* |
| When a scene is heavy | No automatic cuts: *"I've never liked automatic cuts that measure performance, normally leads to a seesaw effect."* Budgets hold the frame. A replay on a real Pi shows zero late frames at every phase gate. |
| Pi and PC | Same rules and same bodies everywhere, so they could even play online together. The PC gets higher resolution, frame rate and visuals, and a wider view. Nothing is made for the PC that can't play on the Pi. |
| The tube | *"hdmi screen likely will never look as good as a tube tv or crt"*. Tube-only tricks are wanted: dithered see-through, interlace detail, signal smear as art, flicker blending. HDMI from the Pi comes later as a middle ground. |
| Which TVs | *"you should be designing this for all tvs, not specifically tuning it to mine. and please dont break my tv"*. First boot on an unknown set: safe mode, then a short setup. |
| AI first | AI writes the code, tests and proves it, and makes assets. No AI inside the running games. |
| Dev console | Someone can make games with just a Pi and their own AI subscription: a very basic, low-cost, factual, updating text terminal on one screen and the game on the other, or SSH as we work now. With one screen, a chatbox or terminal, or split screen. |
| Game code | C plus text files that reload live, with no script language |
| Graphics | *"lets go vulkin."* Vulkan for the new renderer, on every machine |
| Libraries | As now: C with a few small libraries (SDL2, stb, system graphics, now the Vulkan loader), everything else ours |
| Floors | A 4 GB Pi 4. No overclocking. |
| Shipping | Any Pi 4 and PC stores, otherwise not decided |
| Loading | Each game picks seamless streaming or short cuts, and no frame may drop either way |
| Pace | Phases with proof gates, no calendar dates |
| The engine first (11-03) | *"the engine. gaps in the game engine. the engine. the games are secondary to building the engine."* Gaps get filled for the engine's sake, and no game sets their order |
| The PC picture (11-03) | Phase 7 doesn't wait for a game: *"this is one of the most improtant parts. we are developing the engine here. fleshing out the engine."* |
| Pi speed (11-03) | *"really focused more on capabilities and features right now than optimizing for the pi, that will come later on."* Everything still has to play on the Pi; making it fast there comes later |
| What a game decides (11-03) | *"thats a per game question / we need the capability and the feature."* Map size, video mode, character creation and the like are each game's call. The engine's job is to offer the capability |
| Talking on camera (11-03) | Games get moving faces and conversations: *"yes, of course, they need to be able to"* |
| Each game's repo (11-03) | *"of course each game will have its own repo"* |
| The next game (11-03) | *"I think we have brought reva vs zombies up enough to move onto bike lane bash."* Reva vs Zombies stays as an engine test and Phase 2's gate |
| Test hardware (11-03) | *"we are working with what we have."* SD, SSD, a PAL set and a flat panel stay unmeasured until they turn up |
| Jake (11-03) | *"jake is in no hurry to start."* He's on HDMI screens and busy, so the fresh-card test comes later. No push and no kit sync for now: *"we are building the engine right now"* |
| Watching the tube (11-03) | *"both of us"*: jacob at the set, and the webcam once jacob says it's aimed |
| Tube tricks (11-03) | *"whenever you get to them, and be careful with my tv"* |
| GL (11-03) | *"we have already discussed this. i dont like gl. we are moving to vulkin."* Everything moves to Vulkan, film and the old game modes included, and then GL comes out. No new work goes on GL |
| What comes first (11-03) | *"features first, then compatibility, than preformance."* That's the order for judging and reporting all engine work. It doesn't reorder the queue |
| Following this plan (11-03) | *"the plan seems to have been thought out and set up, almost as if we did this together, already, and i had you ask me many questions, and here we are again because you have drifted from it, and we seem to be rewriting the plan instead of following it."* The phases below are the work. Rulings and progress get recorded here, and the order doesn't get rewritten |
| GL (11-04) | *"gl is cancer."* The old renderer comes out entirely inside step 18: every build line on Vulkan, and its code stripped from every file, not left dead behind a switch |
| Sound, music and save (11-04) | A game gets them through the table, as a queue step: *"step after 18"* |
| Film's ragdoll (11-04) | *"i dont see a real reason to refilm the film right now."* Film keeps the ragdoll its takes were made with; step 9's three fixes stay a game's |
| The lighthouse door (11-04) | *"port it."* Door 11 moves to Vulkan with the rest of step 18 |
| The webcam (11-04) | OBS closed, the camera aimed at the tube, the room darkened: it can be read now, and it saw the set's picture |
| A typeface (11-04) | *"whatever we are already using on the pi for the console letters."* Nothing of anyone else's ships: captions and the old door's menu go onto the TV font (`engine/tvfont.inc`), grown a caption size |
| Other languages (11-04) | *"I cant read other languages so I cant verify it, build the capability but leave it inactive for now."* Text beyond ASCII and a way to swap a game's words: a Phase 8 step, built and left off |
| Bike Lane Bash's design doc (11-04) | *"yes"*: read against the queue before the materials step, and anything the engine lacks gets mentioned here, not inserted |
| The dance floor (11-04) | asked again, and jacob: *"we already answered this question."* Ruling 10 stands, and it isn't asked again |
| Committing (11-04) | *"Yes, commit."* Queue steps 4 to 18 banked in one commit (ae82660). Banking stays on jacob's word: *"as before"* |
| Built people (11-04) | On whether step 21's skin, hair, eyes and cloth reach the people a game builds: *"the standard should be high, with the capability to scale down and up."* So they do, at film's standard, with the rungs taking it down for the Pi and up for the PC |
| The kit sync (11-04) | *"you should probably do it now after such a long opus session like we had."* Synced and pushed from ae82660 |
| The skate session's cloth script and wardrobe (11-04) | *"yes"* to both changes as proposed in `design/skate_game_handoff.md` section 3, with the game's own repo left to its session |
| The webcam (11-04) | *"its up to you, I dont need the webcam to see the tv, it is for you. be careful with my tv. please."* It's mine to read; nothing of mine goes on the set outside the in-spec rules above |
| jacob's eyes on the picture (11-04) | *"we need to find a good time for this."* Pending: a short reel on the PC of what steps 13 to 18 built |
| The Pi's old build routes (11-04) | *"yes"*: the old renderer's `kms` and `sdl` routes come out of `build_pi.sh` with it; the direct Vulkan road stays |

## Is low level more efficient?

Jacob's conclusion is right, and the reason needs one correction. C with no script language is the right call: an interpreter would turn 1 ms of game logic into about 70-80 ms, and a field is 16.68 ms. But this engine is already plain C with no script layer, and it ran the zombie game at 4.4 fps. Only 8% of its CPU time was the engine's own code. 83% was the driver, the kernel and copying, doing what the engine asked for. **Going lower gains almost nothing from here. Asking for less gains everything.** One film mesh on a game path did more damage than any language could.

The correction is that high-level layers aren't only for humans. The ones that make things comfortable can go, such as class hierarchies, script sugar and reflection. The ones that check code matter more with an AI writing it, because memory errors lead the measured defects in LLM-written C, and agents erode their own code over time. So we keep a small header, handles, assertions, sanitizers and replay tests, all in plain C.

## What the Pi told us

All measured today on a 4 GB Pi 4 at stock clocks, on composite, into one NTSC set.

| What | Result | What it means |
|---|---|---|
| Output timing | 480i 59.941 Hz, 240p 60.055 Hz, 0 late of 600 flips | Steady enough to lock the game to |
| Flip deadline | Lands up to 16 ms after the last flip, always misses at 17 | Almost the whole frame is usable |
| Hiccup | One missed flip at a 13 ms wait, with a compile on another core | The frame thread needs real-time priority and margin |
| 240p to 480i switch | 56-60 ms, a brief roll, and this TV's "LINE2" label pops up | Behind a cut or fade only |
| Field parity | Alternate flips stay locked, and one repeated flip shifts them | Interlace tricks are possible; how to learn the phase at start is open |
| Bare GPU, 480i | About 10.5 ms per million indices, 11 microseconds per draw, blending no dearer than opaque | Geometry and draws are cheap. These are upper bounds, from a trivial shader |
| 400 bodies of 800 triangles | 13.2 ms with own bones, 9.3 ms instanced | The hardware is not the limit |
| Same frame on GL and Vulkan | Vulkan within about 10% almost everywhere, with less CPU: proof frame A 11.2 ms against 10.5, B 21.2 against 17.9, C 33.3 against 34.5 | Vulkan isn't far behind. The choice stands |
| Real frame, by shader strength | Same geometry: 4.9 ms of pixel work with a budget shader, 13.5 with shadows, 29.3 with today's six lights per pixel | The shader is the big lever |
| A frame live | Proof frame B has 17.9 ms of work and still holds 60 with no late frames | Vertex and pixel work overlap across frames, so each gets its own budget, not their sum |
| Dance floor, 4 bodies | 106 of 2100 late in the first run, 0 of 5400 with citizens, 1 of 37,495 over 10 minutes | The count isn't the cause. Late frames came only with 350k+ indices on screen, apart from that one |
| Four-way split screen, 100 bodies a view | 0 late at 240p on Vulkan, 86 of 599 at 480i | Holds at 240p. At 480i the views share the body budget |
| Zombie game, Kaeli as hero | 4.4 fps, 9.5 M indices, 68 ms CPU | One film mesh on a game path |
| Zombie game, citizen as hero | About 30 fps, 0.5 M indices, 12 ms CPU, 100 or 400 slots alike | About twice away. The crowd isn't what's left: it's the sun shadow pass (262 of 331 draws) and the engine's one large shader |
| Real-time priority | No measurable change, with or without a compile running beside it | Cheap to keep, but not what's holding anything back |
| Same game on Pi and PC | Hashes differ from the first checkpoint, on identical packs | Not explained yet. Phase 1 has to fix it |
| Memory | All packs never reach a first frame. The game's own 77 leave 1.9-2.4 GB free | A game loads only what it needs |
| Rebuild on the Pi | 197 s optimized, 18 s to compile unoptimized | A Pi developer needs a prebuilt engine and a small game unit |

## How it's designed

**The frame holds by budget and schedule, and never reacts to itself.** The loop is a fixed schedule per game and mode: flip, fixed wait, input, one tick, submit, flip. A late frame is a break in the flip sequence, not a low average. Quality settings are one text table chosen at setup and never moved by a timer. `REVA_TIER=auto` goes away.

**On the tube, a budget is three numbers: CPU, vertex work and pixel work.** Each has to fit in a field on its own, because they overlap across frames, and each gets a 13 ms ceiling for margin. The shader's strength is part of the budget, since it moves the pixel side further than anything else. Measured on the Pi 11-02, through the Vulkan direct path at 480i (ms):

| Line in the frame | Indices | Draws | CPU | Vertex | Pixel |
|---|---|---|---|---|---|
| Clear and empty frame | | | 0.23 | 0.04 | 0.87 |
| 100 citizens and a 20k-triangle hero | 320k | 101 | 1.74 | 1.66 | 2.66 |
| Street, ground, two HUD layers | 194k | 103 | 0.72 | 0.87 | 2.23 |
| **Proof frame A: budget shader** | **514k** | **204** | **2.69** | **2.57** | **5.76** |
| Add one shadow pass and a four-tap lookup | | +101 | +1.16 | +4.46 | +8.72 |
| **Proof frame B** | | | **3.85** | **7.03** | **14.48** |
| Add six lights per pixel | | | +0.43 | +0.01 | +11.69 |
| **Proof frame C** | | | **4.28** | **7.04** | **26.17** |

Live, proof frame A has no late frames and plenty of room. B has one late frame in 299 at 480i with the pixel side at its limit, and none at 240p. C doesn't fit at either. So the game budget starts at A with a little of B: a cheaper shadow than four taps on everything is where the leftover goes, tested when the renderer exists.

**One body path, budgeted at the cook.** Crowd slot, scene cast and set actor become three owners of state with one `bodyPose` and one `bodyDraw` (the plan in `engine_capabilities.md` section 21), built as fixed pools with generational handles and no ECS library. A cook step refuses a game pack over its budget, so a film mesh can't reach a game path by accident. Instancing with shared poses is the crowd setting for when counts go up.

**The simulation writes a packet and never reads the renderer back.** This is the N64 lesson in usable form: the game writes the frame as data, and something else draws it. The packet carries stable IDs, a reset flag and a camera-cut count, so we never need RT64's guesswork or Zelda64Recomp's 284 KB of tagging patches. A hash of the packet gives an AI a text diff before any pixel check.

**Sixty-hertz ticks everywhere, one at a time, identical on x86 and ARM.** Built in Phase 1: a frame that owes k ticks passes through the loop k times and draws only on the last, so the rules don't change with the frame rate. The rules for matching across machines are `-ffp-contract=off` on every build, our own sin, cos and atan2 (`club_math.c`), and hashing fields rather than struct bytes. `qsort` is only used where equal keys can't change a result. Windows x86 and the Pi now agree bit for bit. Interpolation runs only on displays faster than 60 Hz, so until then a 100 Hz PC monitor shows 60 new images a second.

**The display layer boots safe, follows the standards and asks the player.** The engine only emits a whitelist of the four standard composite modes, so it can't produce an out-of-range scan rate, the one kind of signal damage on record. A mode from another region runs only after a button press answers a voice prompt. First boot uses the regional interlaced mode. 240p is offered with a 15-second auto-revert. Recovery works with no picture at all: a button held at power-on, a file on the boot partition, voice prompts. The picture aspect comes from the standard (the 720 raster is 15:11, and its central 704 samples are 4:3), with safe boxes of 90% and 80% until the player adjusts them. Nothing is tuned to the Toshiba, and a measurement from it is a check on the standard, never the design value. Tube tricks are optional art, never carrying play information, each with a plain fallback.

**Vulkan 1.3 on every machine, with our own thin layer in C.** jacob, 11-02: *"lets go vulkin."* 1.3 is the floor because it's what the Pi's driver has, and nothing is made for the PC that the Pi can't play. Features above the floor are PC settings only.

| | Floor: every machine (read from the Pi 11-02) | PC extras, by setting (GTX 1070 has these) |
|---|---|---|
| Version | 1.3 (V3DV, Mesa 25.0.7) | 1.4 |
| Core tools | Dynamic rendering, synchronization2, timeline semaphores, buffer device address | The same |
| Textures per shader stage | 16 | About a million, with bindless |
| Push constants | 128 bytes | 256 bytes |
| Draw batching | Instancing, one draw call at a time | Multi-draw indirect |
| Colour attachments, max image | 4, 4096 | 8, 32768 |
| Texture formats | BC1, BC3, ETC2, ASTC | BC1, BC3, BC7 (so BC1 and BC3 are the shared pair) |
| Shader integers | No 16-bit or 64-bit | Check per feature |
| Memory | One 1.85 GB heap, and the CPU reads it uncached, so never read back on a frame path | Separate device memory |
| Top of the ladder | | Ray tracing is listed, and it's a setting on a trigger |

Details of how it's built:
- **The tube:** render straight into scanout buffers imported as `dma-buf` images, and flip them ourselves through KMS, with no SDL or window system in the way. Tested 11-02 with the real frame at both modes. It costs a little less than `VK_KHR_display`'s swapchain, which also works and is the fallback.
- **The PC:** a Vulkan surface on SDL2's window. SDL also stays for audio and pads. FIFO ran unpaced in a window on the GTX 1070 (3,640 presents a second), so the PC renderer can't trust it to pace until that's understood.
- **The late-frame counter:** the Pi's driver has no present-timing extensions, so the swapchain can't report flip times. The direct path gets the kernel's own flip timestamps, the same as the GL road.
- **Shaders:** GLSL compiled to SPIR-V at build time and shipped that way, with no shader compiling while a game runs. Pipelines are all built at load, so nothing compiles mid-play.
- **No extra libraries:** C against the plain Vulkan headers. No Vulkan Memory Allocator (it's C++), since the fixed pools below do that job.
- **The old GL path stays** for film and the legacy doors only until each one is ported, which is step 18 of the queue, and then it comes out. Film must keep working throughout. No new work goes on it. jacob, 11-03: *"i dont like gl. we are moving to vulkin."*
- ⛔ Vulkan alone changes nothing in the picture. The look comes from the graphics list in Phase 7, and the API only makes it reachable.

**Virgo teaches this renderer, but isn't this renderer.** Virgo is jacob's own Vulkan renderer for the N64's RDP (`D:/Truellen64/virgo`, Rust on `ash`). It recreates one old machine exactly: its input is RDP commands, its fragment shader was 13,196 instructions on the Pi, and a drawing field took 65 ms there against our 16.68. It stays its own project, and we take from it: `V3DV_FACTS_20261022.txt` and the plan's rung 8 (what the driver has and doesn't, what V3D's compiler does to loops and `discard`), and the shape of a one-way command stream behind a small C header (`virgo_rdp.h`).

**Pin the driver.** Pin the Pi OS image and Mesa version per release, and log the driver version with every number. Vulkan 1.3 on the Pi needs Mesa 24.3 or later.

**One small header and two tables separate engine from game.** Quake II's shape: a function table each way and a version number. Add an append-only rule, an engine-owned loop, and a `draw` that takes `const void* state`, which enforces the packet rule in C. An AI working on a game needs one header and one game file, a better limit on context than any style rule. Today `kit/manifest.txt` sends the whole of `crowd_kms.c` to aigameengine, game modes included, so the two-repo rule isn't met until this lands.

**Fixed pools and arenas now, threads when a budget demands them.** 83% of the zombie game's CPU was driver, kernel and copying on one GL context, which more threads can't reach. Count bytes per category and refuse content past the ceiling. An `ARENA_DEBUG` build makes every push a real `malloc`, or arenas blind the sanitizers.

**Packs become chunked, compressed and scoped to one game.** The Pi and PC both advertise BC1 and BC3, so one compressed texture can serve both. The format grows by appending a directory and footer, so old bytes never move, and faces, morphs and clips become chunks a rebake can't lose. Each game's cook produces a manifest, and refuses missing files, surplus files and anything over budget.

**AI writes and proves mechanism, but doesn't grade itself or judge the look.** The gate has five layers: an exact sim hash; a structured render report; exact image baselines per configuration; an LLM reader given a blessed reference, run by a separate agent; and jacob's eyes. LLM vision alone scores 43-45% on real glitch tests, so it's never the sole judge, and agents don't own their own baselines. Every bug gets a tape before its fix.

**Data, tests and a pinned floor are what outlast the code.** A written pack spec, golden packs, `-std=gnu17` on every build line, a `.gitattributes`, a dated toolchain copy, permissive terms when it goes public. Pi 4 production runs to at least January 2034, which is a natural horizon for the floor.

## The queue

jacob approved this list on 11-03: *"ok, make sure you follow this, and dont srift, its compaction thats causing it."* It's the phases in numerical order. Work it from the top, one step at a time, and tick each step here when it's done. Nothing gets reordered, added or skipped except by jacob's word. No Pi runs or Pi numbers along the way: *"we are building the engine. we can fine tune it for the pi later."* After a compaction, read this list before doing anything else.

**Phase 2**
1. [x] Finish what Vulkan doesn't draw yet: the sky, menus other than pause, the jumbotrons, the floor's contact shadow, the crowd's coloured lights, post, sheer alpha, and cloth's special cases.
2. [x] Instancing and atlases.

**Phase 3**

3. [x] Faces, morphs and clips as pack chunks.

**Phase 4**

4. [x] The reading-size step.

**Phase 5**

5. [x] The chatbox.
6. [x] Shrink `CLAUDE.md` toward 200 lines.

**Phase 6**

7. [x] Creating and removing objects while a game runs.
8. [x] The animation graph on every body.
9. [x] Physics that games can reach.
   Between 9 and 10, on jacob's word (11-04): the engine's compiler warnings, cleared. Phase 1's table says what that found.
10. [x] A fuller UI toolkit.
11. [x] Particles in split-screen views, and each player's own text.
12. [x] Far detail, and bodies that stream.
13. [x] Long-session memory.
14. [x] Moving faces and conversations for games.
15. [x] Building a body while a game runs.
16. [x] Changing level mid-game, pathfinding, props on any bone, and a music clock.

**Phase 7** (steps 1 to 3 are built)

17. [x] The toon pass on Vulkan.
18. [ ] Film and the old game modes onto Vulkan, then GL comes out (jacob's word, 11-03, above; and 11-04, *"gl is cancer"*: its code stripped from every file, not left dead). It sits here because film needs the faces in step 14 and the toon in step 17.
19. [ ] Sound, music and save in the table: `E->sound`, `E->music`, `E->save` and `E->load`, and `club.h` out of `engine.h` bar the math a deterministic game must use (added on jacob's word, 11-04: *"step after 18"*).
20. [ ] Step 4: material data, specular with roughness and metal, reflections, surface detail. Before it (jacob, 11-04): `design/bike_lane_bash.md` read against this queue, and what the engine lacks mentioned here.
21. [ ] Step 5: skin, hair, eyes, cloth.
22. [ ] Step 6: particles, decals, level of detail, water, foliage.
23. [ ] Step 7: 100 and 144 Hz, resolution and widescreen, the flat-panel look, depth of field, motion blur and TAA, HDR output.

**Phase 8**

24. [ ] Text beyond ASCII, and a way to swap a game's words for another language's: built and left off (added on jacob's word, 11-04).
25. [ ] Licence, notices, pinned toolchain, fuzz-tested file readers, packaging, a Pi image.
26. [ ] Online play.

**Not in this sequence, because they wait on jacob's word:** the kit sync and push, the fresh-card test, the PAL work, SD and SSD, tuning for the Pi, and the gates' Pi replays. Committed on his word 11-04. The webcam is aimed at the tube since 11-04; what it watches there waits for the Pi's turn unless jacob says.

**Beside the queue, from the skate game (11-03).** jacob: *"I'd like you to take it from here and get it into the aigameengine kit. Don't drop what you're doing; fit it into your queue where it makes sense."* `design/skate_game_handoff.md` says what it is. Committed in reva as dc51c0b. What's still open: section 3 of that note, and the sync and push, on jacob's word: *"Don't push to the kit until I say."*

**Beside the queue, from the film session (11-03).** `design/pdoom_engine_notes.md`, and jacob: *"It doesn't change the queue's order."* Read its section 3 before starting each of steps 2, 14, 16, 20, 21 and 22, and use its section 2 as step 18's checklist. Its section 4 isn't on the queue: *"Don't build it unless I tell you to."* The note goes in the next bank.

## Review, 11-04

jacob, 11-04: *"take a good look at this project, let me know how we are doing. anylyze the plan, look for gaps, ensure we have done work, and write the plan up to proceed, double check it, then ask me some questions before we proceed."* This section is that review. It adds nothing to the queue: a gap here is a proposal until jacob says.

**Where it stands.** Queue steps 1 to 17 are built and ticked, and step 18 is in progress ([Phase 7](#phase-7-the-pc-picture) says what's across and what's left). The tree: queue steps 1 to 3 are committed as 35a3990, and another session added Bike Lane Bash's design doc as 967eccc. Everything since, queue steps 4 to 18 and the warnings cleared, was uncommitted until the bank of 11-04, on jacob's word: 141 files changed or new, 49 of them new, in one commit. Nothing is pushed. The kit's last sync (a7cd144) is from 87f5b6c, before any of Phase 7. Nothing since 87f5b6c has been compiled for the Pi or Linux: steps 4 to 18 are proven on the PC, as the order allows, and the cross-build is on the commit list.

**The work is there.** Each ticked step's pieces are in the tree: `engine/objects.inc`, `animgraph.inc`, `ui.inc`, `gamefx.inc`, `stream.inc`, `face.inc`, `people.inc`, `gameworld.inc`, the games `rocks`, `tower`, `herd`, `roam`, `talk` and `people`, the checks `objcheck`, `graphcheck`, `physcheck` and `uicheck` (which need kit manifest lines at the next sync), and their receipts' reference lines. Film's test take renders on Vulkan and reads like its reference. **The receipts, run again for this review (11-04):** the zombie game with and without a tape, the dance floor, the reading-size runs, and the sample, rocks, herd, roam, talk and people games all print their stored lines, at every tick pattern; the tower doesn't (gap 2 below). The warning check found three new warnings, all from step 18's patches of the same day, and they were cleared. For the bank: the zombie game at the `pi` rung, one frame from the commit before and one from this tree, is 0 of 921,600 pixels off, and the Vulkan engine compiles for the Pi in the container (64 s), not sent over.

**Gaps found on the way.** Mentioned, not inserted.
1. **Sound, music and saving reach a game past the table.** `engine.h` includes `club.h`, and every game calls `sfxPlay`, `sfxPreload` and `musicPlay` straight from the mixer; nothing in `EngineAPI` plays a sound, starts music or saves a game's progress (the lighthouse's RESUME GAME says *"nothing to resume until the game can save"*). The plan's boundary is the two tables. The fix is small and append-only: `E->sound`, `E->music`, `E->save` and `E->load`, with `club.h` left in the header only for the math a deterministic game has to use.
2. **The tower's receipts differ from their stored lines** (`tower_noinput` and `tower_tape`, both first at tick 60, while every tick pattern agrees with itself, so the rules are still exact). It most likely moved when step 9 put physics on its crates and the references were left for commit time, but that isn't proven. Find why before blessing; mine, at commit.
3. **Film's ragdoll.** Step 9's three fixes for a game's limp bodies (real friction, corrections by mass, the fold) are off for film, whose takes were made without them. Whether film gets them is jacob's call.
4. **A streaming world has no path map:** `E->path` searches the columns live, with a budget. A map per cell is the follow-on, if a game needs it.
5. **Built people aren't film's:** shell hair, a painted face, mitten hands, no garments. Step 20 is the place to decide whether they get skin, hair, eyes and cloth too; the plan says realistic bodies only.
6. **Conversations:** a seated body's shot sits too high, and split screen has no conversation lens of its own.
7. **No font of our own ships.** The captions and the lighthouse read Windows' Segoe or Linux's DejaVu and fall back to the segment font with neither. Step 23's packaging needs a typeface with a licence that allows it, or the TV font grows a caption size.
8. **"GL comes out" can mean two things:** every build line on Vulkan with the old libraries no longer linked, the old code left dead behind a switch (about a day), or the old code stripped from every file, about 1,300 call sites across 24 files (several days, the receipts holding throughout). Question below.
9. **The PC's frame pacing:** one frame in five late in a 1280x720 window on a 100 Hz panel, measured 11-03 and not looked into. Step 22's gate has to answer it.
10. **Still open from before:** ruling 10, the dance floor in the engine; the CLAUDE.md shrink's one undone suggestion, a law with no exceptions as a hook rather than prose; `design/engine_capabilities.md` has two sections numbered 38; ruling 8, online play between refresh rates, defaults to Phase 8.
11. **Not in the plan at all:** text beyond ASCII and other languages. The TV font is 8x8 ASCII and the captions take UTF-8. Public someday means other languages someday; a scope question for Phase 8.
12. **The webcam** can't be read: `obs64.exe` holds the camera, and OBS's virtual camera shows a placeholder.

**Waiting on jacob, asked before and not answered:** the skate session's cloth script and wardrobe (section 3 of `design/skate_game_handoff.md`), the kit sync and push, film's 29 UNDECIDED manifest files, promoting the exes, and section 4 of `design/pdoom_engine_notes.md`.

**The way on, as the queue has it:** finish step 18 (the lighthouse door, the build lines, the old renderer out, one look at each piece the take doesn't show, the write-up, the tick), then 19, 20, 21 and 22 (section 3 of the film notes read before each of the last three), then 23 and 24. jacob's answers below may change it, and nothing else does.

**Questions put to jacob (11-04):**
1. Commit now? Steps 4 to 18 are a lot of work on one disk. A bank now, or one per few steps, protects it and pushes nothing.
2. "GL comes out": the builds only now and the dead code stripped as a Phase 8 cleanup, or all of it inside step 18?
3. Sound, music and save in the table (gap 1): a step after 18, or left as it is?
4. Film's ragdoll: the three fixes, or film as it was shot?
5. The lighthouse door: port it (written, one build away), or leave door 11 on the old renderer and let it go with the old code? It's reva's, never the kit's.
6. The webcam: close OBS so the camera can be read directly, or make the camera OBS's virtual camera? And is it aimed at the tube?
7. Step 20's scope: built people too, or realistic film bodies only?
8. A typeface of our own for the Pi image: DejaVu, or one jacob picks?
9. Other languages: a line in Phase 8, or not this engine's job yet?
10. The dance floor: stays in the engine, or behind `engine.h` before going public?
11. Bike Lane Bash's design doc (967eccc): should it be read against the queue for what the engine lacks, before step 19?

**jacob's answers, 11-04,** each now a row in the decisions table and, where it adds work, a line in the queue: 1 *"yes"*: banked in one commit, nothing pushed. 2 *"2 needs to happen, gl is cancer"*: the old code stripped inside step 18. 3 *"step after 18"*: queue step 19. 4 *"i dont see a real reason to refilm the film right now"*: film's ragdoll stays. 5 *"port it"*: the lighthouse door. 6 OBS closed, the webcam aimed at the tube, the room darkened: the camera read the console's menu off the glass. 7 *"not sure what you mean"*: asked again below in plainer words. 8 *"whatever we are already using on the pi for the console letters"*: the TV font. 9 *"build the capability but leave it inactive for now"*: Phase 8's step 24. 10 *"we already answered this question"*: ruling 10 stands. 11 *"yes"*: before step 20. The four checks that came with steps 7 to 10 were run for the bank too (objects 110, graphs 27, physics 134, menus 50, none failed), the tower was blessed once its why was known (its own game changed after its last bless: loose crates, a lift and beacons in step 9 and a menu in its state in step 10, all in the rules; no other game's receipt moved and every tick pattern agreed), and the warning check was clean.

## The phases

jacob, 11-02: *"we are not trying to do preformance on the pi right now, we are trying to flesh out the engine, on the plan we had written up. yes the preformance wins are nice, but they shouldnt block us from fleshing out/building/finishing the engine. we have alot of gaps to fill."* So building comes first. Each frame-timing check below gets run and reported, and a late-frame count doesn't hold up the next phase. No dropped frames stays the proof game's target.

**The gates' Pi replays wait for performance's turn.** jacob, 11-03, asked whether they should, under *"features first, then compatibility, than preformance"*: *"Yes, move them."* So a gate below is met on what it says about features. Wherever it names a replay on the Pi or late frames, that part runs when performance's turn comes, all of it together. It's then a tape replay on a real Pi with zero late frames, on the board we test on at its stock speed (a Pi 4 Rev 1.4, 1.8 GHz). jacob, 11-02, on also testing at the older boards' 1.5 GHz: *"nah i dont want to do that right now. my friend uses the exact same model we are using. this is good."* Replays run 1 minute, the proof's included. jacob, 11-02, on the 3-hour soak this plan first asked for: *"3 hour soak? thats ridicoulous"*, and on the 10 minutes after it: *"should make it 1 minute."*

### Phase 0: truth on the board (measured 11-02)
| Item | Result |
|---|---|
| Late-frame counter and per-pass GPU timers | In the engine: `[frames]` on every stat line, `REVA_PASSTIME=1` or `2` with vertex and pixel time per pass on the Pi. `REVA_SEED` and `REVA_NOHOLLY` came with them. |
| What the last 2x is | The sun shadow pass and the engine's one large shader (see "What the Pi told us") |
| The 4-body drops | Heavy bodies on screen, not the count |
| A real citizen, texture, shader, shadow in the bench | `v3d_lab2.c` |
| The bench on Vulkan, compared with GL | `vk_lab.c`. Not far behind, so the choice stands. The budget table above comes from it |
| Vulkan in the Windows build | MSYS2's `vulkan-headers`, `vulkan-loader` and `glslang`. `vk_pc_test.c` opens a Vulkan window |
| Real-time priority | No measurable change |
| One tape's hash on Pi and PC | They differ from the first checkpoint. Moved to Phase 1 |
| The 240p aspect bug | Fixed: the picture's aspect comes from the standard, 15:11 |
| Repo hygiene | `.gitattributes`, `-std=gnu17`, `-ffp-contract=off` |

**Gate:** a budget table from real numbers (above). Replay: 4 bodies, 10 minutes at 480i: **1 late of 37,495 flips**, a 33.4 ms gap with only 2.7 ms of frame work around it, so something outside the frame. Reported, not chased, under jacob's 11-02 word above. The next phase's replay will show whether it recurs.

### Phase 1: the contract (built 11-02)
| Item | Result |
|---|---|
| Split `crowd_kms.c` as text | Done. 33 parts in `engine/`, and `crowd_kms.c` is a 171-line table of contents. The compiled object is byte-identical. Still one translation unit |
| A no-render tick loop | Done, and more than that: `-DREVA_HEADLESS` with `gl_null.c` runs the sim with no GPU, window or vsync, at about 2,400 ticks a second on the PC and 700 on the Pi |
| Single-step ticks | Done (`clockFrame`, `g_noDraw`). A frame drawn every second tick is 0 of 921,600 pixels off one drawn every tick |
| A tape keyed by tick | Done. v2 is one line a tick, floats as exact bits, the seed in the header. v1 tapes still read |
| The float rules, own trig | Done (`club_math.c`). The last difference between the machines was the hero's x, one bit apart at tick 247 |
| Why Pi and PC differed | Two things. The Pi's copy lacked `v1_mob1.rpack` and `v1_kaeli.rpack` (data, not machine), and then the math libraries' last bits. The display road didn't matter |
| The `auto` tier and tick | Gone. `rvz.bat` runs 60 ticks on any display |
| Mode selector and whitelist | Done at boot, on the KMS road: `REVA_VIDEO=480i\|240p\|576i\|288p`, and composite emits nothing else. Switching mid-game waits for the Vulkan renderer. The 15:11 constant came in Phase 0 |
| ASan and UBSan over tapes | Run on Linux x86 in Docker, because ASan can't start on this Pi: its kernel's 39-bit address space doesn't fit ASan's allocator. No memory errors on the street game's tape or the dance floor. UBSan finds only unaligned reads in the pack loader, which Phase 3's pack format removes. `MALLOC_PERTURB_` runs are clean |
| No compiler warnings | Done 11-04, on jacob's word. `warncheck.py`: none in any way the engine is built, on the PC's compiler and the Pi's, and a new one fails and says where. It was a count of 251 that wasn't allowed to grow. Counting every build there were 364 places. 209 line breaks changed no compiled byte. 37 were float pointers into a pack's node table, which a pack doesn't promise is aligned: it's copied into memory that is when it loads. Reads through other casts into a pack are still there and the compiler can't see them, for step 23's readers. A few names that were cut off silently have room now. Reading all of them found one small bug, a reset that cleared 8 of 128 face shapes. |
| The receipt | `driver.py check` (`hashcheck.py`): three scenarios and odd cadences in about 10 seconds |

**Gate:** one tape gives the exact hash on Windows, Linux x86 and the Pi: **met**, on the real street game, with all three check scenarios on all three, and 601 checkpoints over 36,000 ticks between Windows and the Pi. A simulated 50 Hz cadence gives the same hash: **met** (`REVA_CADENCE=1,1,1,1,2`). Replay: 40 citizens, 10 minutes each: **1 late of 37,495 flips at 480i, 1 of 37,618 at 240p**, reported and not chased. The first came in the opening seconds and the second about 15 seconds in, both with only a few ms of frame work around them.

### Phase 2: the proof game
**How the renderer comes across.** Under `-DREVA_VK`, `gl_null.c` stands in for GL: `glBufferData` and `glTexImage2D` pass through to `gfx_vk.c`, named by the GL names the pack loader already holds, and every other GL call does nothing. Draw sites then hand their work over as data through `gfx.h`, one site at a time, and whatever hasn't come across simply isn't drawn. The sim can't tell: the Vulkan build prints the same hashes as the GL build. The GL build is untouched and stays for film and the old doors. The street game only has about a dozen live draw sites, which is why this is small.

**Where it stands (11-02).** The zombie game and the dance floor both run on Vulkan on the PC and on the Pi's direct road, with the same hashes as GL. Numbers are in `pi_measurements.md`, reported and not chased, as above.

**What was left to draw is drawn (11-03).** On Vulkan now: the old sky's dome, by rung (`sky` in `gfxset.inc`), every menu, the jumbotrons with Holly's head and the live feed, the floor's contact shadow from the sun map, the crowd's coloured lights, sheer alpha and glow, hanging cloth and set cloth. Post is Phase 7's HDR road, and `low` uses MSAA in place of FXAA. All of it is off at the `pi` rung, 0 of 921,600 pixels off, and the receipts held. Not seen yet: a sheer `#rrggbbaa` (no prop on disk names one, and the packs weren't searched), set cloth, and the live feed in a shot. The film's vignette and the space sky wait for step 18 of the queue.

**Instancing and atlases (11-03).** The renderer now puts every copy of a mesh in one call. The packet lists draws as it always did and doesn't know, so the game can't tell and the hashes can't move. Each copy brings its matrix, colour, texture layer and where its bones start, in a stream of its own, and a stretched mesh's normals come from its matrix's cofactors, which film's movers need. A game's pack textures move onto shared pages once it has loaded, one page per size and format and each texture a layer, so repeat and mips work as they did and no UV moves. What streams in stays on its own. `merge` and `pages` in `gfxset.inc` are the off-switches, and a screenshot prints what its frame cost.

| One frame at the `pi` rung | Draws + sun | Calls, off to on | Texture binds, off to on |
|---|---|---|---|
| The zombie game | 60 + 196 | 256 to 56 | 39 to 5 |
| The horde: 100 bodies, most of them different packs | 107 + 253 | 360 to 144 | 100 to 2 |
| The herd: 600 of one body, 153 in view | 153 + 32 | 185 to 4 | 4 to 2 |
| Film's stage: 57 stretched meshes in view | 97 + 4 | 101 to 24 | 21 to 5 |
| The dance floor | 6 | 8 to 8 | 6 to 3 |
| The tower | 3 + 2 | 5 to 4 | 3 to 2 |
| The streaming world | 12 + 13 | 25 to 24 | 12 to 9 |

Each of those is 0 of 921,600 pixels different with the two settings off against on, and the `pi` rung is 0 off the commit before. So are split screen, the tower at `high` and film's stage at `high`. At `high` the zombie game differs in 3 pixels, by at most 11 levels, where surfaces tie for depth; the same build twice is 0. Those frames were all taken on the PC.

**Shared poses** are the crowd setting. `E->tune->poseRate` is how many poses a second of clip a crowd's far bodies share, and `E->tune->poseNear` is how many of the nearest keep their own (`engine/poses.inc`; `REVA_POSERATE` and `REVA_POSENEAR` for an A/B). In the herd 150 bodies stood in 37 poses, and the packet's bones went from 247 KB to 97 KB. The rules can't see it: `[simhash]` holds with it on in the herd, the zombie game with and without a tape, and the dance floor, and only `[packet]` parts. It's off unless a game asks, and how far bodies look at that rate is jacob's to judge.

**2D.** A game's small images go on one sheet at the first draw (`engine/sprite.inc`), so they don't break a batch. The rocks game drawn from four images against its one sheet: 0 of 921,600 pixels off, and the same rules.

**Not done or not seen.** None of this has run on the Pi. Bones in a storage buffer read from the vertex shader, array textures and copying compressed textures are all inside the limits V3DV reported (`vulkaninfo_reva_20261022.txt`), so it should run there: that's for when compatibility's turn comes. Pages save binds and not memory: film's stems still cost their 2048 atlases. The sun's crowd casters are still capped at 32, an old limit the merge may make room to lift.

| Item | State |
|---|---|
| The packet | **Built** (`gfx_packet.c`). The frame is plain data with stable ids. Vulkan draws it, and the no-display build hashes it, so `driver.py check` covers what the game asks to be drawn, with no GPU. Vulkan's packet and the no-display build's match at every checkpoint, Windows, Linux x86 and the Pi all print the same packet hashes, and it changed 0 of 345,600 pixels. `REVA_PACKETDUMP=<tick>:<file>` gives a frame as text to diff |
| The one body path | First layer done (`engine/body.inc`): the rest pose, FK, palette, vertex streams and the draw call are each written once, and the hero, crowd, cast and actors all call them, so their meshes all reach the packet. Same hashes, and 0 of 921,600 pixels off on GL with a hero, a partner and 33 actors in frame. Second layer: the scene cast and set actors share one mesh loop (`bodyMeshes`), same proof. Third: the hero came onto it too, 11-03, with 0 of 921,600 pixels off on the three A/B shots. The crowd keeps its own loop on purpose: each of its meshes picks a program and its lights and runs the squid check, it's built from the same steps, and it reaches the same packet draw, so a new renderer feature is still only written once. Clips nearly all come through one sampler (`clipSampleF`). Still separate: the cast's nearest-frame sampling, because merging it changes film and needs jacob's eye |
| Zombie game and dance floor through both | **Done.** The floor is data now (`engine/venue.inc`): what GL's large floor shader works out per pixel is worked out per tile on the CPU, so a pixel of it costs next to nothing. The packet gained general effect triangles and glowing sky points, and the renderer the hologram. Side by side with GL at the same tick it reads the same. The venue's readouts go on the TV's grid on composite |
| A game-grade Kaeli | **Done** (`skate_src/author/cook_game.py`): 1.06 M triangles and 126 MB to 20,000 triangles and 6 MB, same skeleton, same clips. Same hashes as her film pack, and side by side at 480 lines and 1440p she reads the same. The film pack is untouched |
| The Vulkan renderer | One forward pass and pipelines at load: done. Instancing and atlases: done 11-03, above. The 100 bodies held at 240p without them |
| Cook budgets, per-game manifest | **Done** (`games/rvz/game.txt`, `anim_bench/cookcheck.py`). The list refused 53 files before the cook and none after. `--stage` makes the game's own folder, 132 files and 349 MB, and `--verify` boots it: same hashes as the film version, and it reads exactly what's listed. `REVA_OPENLOG` is what makes that exact |
| Frame arenas | Measured first (`anim_bench/alloccount.c`): **the game's frame path already asks for no memory**. 3,875 allocations at boot, and the same 3,875 after 36,000 ticks, 10 minutes of play. The one exception was a sound loading on its first play, which now loads before play. The packet's arena is the only frame memory: one fixed 16 MB, loud past the ceiling. Nothing more needed for this game |
| A HUD you can read at 480i, inside the safe box | **Built** (`engine/tvtext.inc`, our own 8x8 font in `engine/tvfont.inc`): a 35 x 24 grid inside the 80% box, whole pixels, the same layout at 480i, 240p and on a PC. Not yet seen on the tube by jacob |

**The proof so far**, on the Pi from the game's own folder, the cooked Kaeli, 10 minutes each at 240p, the governor left alone. The normal game: **0 late of 36,682.** The horde (`rules/rvz_horde.rules`), 100 bodies in view: 0 late for its first 5.7 minutes bar 5, then 1,517 late of 34,860 in all. Each frame's CPU still averages 9 ms, and the slip begins when a frame crosses about 160 draws and 500,000 indices, with another 430,000 in the sun pass, so that's the frame's weight, not yet split between CPU and GPU. At 480i it's further off: the normal game's later levels put the pixel side at 99%, 139 late in two minutes, and the horde 356 of 4,708 in its first 90 seconds. The sun pass is the first place to look when it's time: it still draws 250 things with no frustum test. The dance floor with 40 citizens: 0 late at both modes.

**The late frame every 18 seconds is gone.** jacob, 11-02: *"its probably from the pi throttling itself cuz it thinks its not working and should conserve power."* That was the trigger: holding the CPU clock up took it from 7 late to 0. The repeat was the tick clock's. One frame jittered by a clock change left it short of a tick, it slept, and each sleep timed the next frame from its own end, which locked the loop to 60.000 Hz against a 60.055 Hz field and lost a field every 1 / (60.055 - 60) = 18.2 seconds. On the tube the flip is now the only pacer. The governor can do as it likes: 1,646 clock changes in the 10-minute replay above.

**Gate:** Reva vs Zombies, 100 bodies, on the tube, a 1-minute replay at stock speed, and jacob's eyes on the glass. jacob, 11-03, on who watches: *"both of us."*

### Phase 3: the boundary
- `engine.h` and two tables. The zombie game builds against the header alone. **Built 11-02.** `games/rvz/rvz.c` includes `engine.h` and nothing else of ours. `EngineAPI` is what the engine offers, `GameExport` is what a game does, there's a version number, and `gameEntry` is the one thing a game's file must define. A game's tick writes its state, and its `draw` and `hud` get that state `const`. The engine asks `GAME_ON` and never which game. The proof is that nothing moved: the receipt prints the same sim and packet hashes on Windows and the Pi, seven paths through the game are byte-identical before and after (no input, a tape, the horde, the old prototype's rules, the yard, the arcade's rotation door, the 240-line raster), and it played 0 late of 4,175 at 240p on the Pi from its own folder.
- The other game modes wait in a `legacy_doors` unit in reva, unported until someone works on them. **Moved 11-02.** `legacy_doors/` holds their frames, their setup, the platformer's body, the mode select and two HUD fields, moved as text with nothing rewritten: about 1,000 lines came out of the engine's files. The engine's remaining lines about them sit under `#ifdef REVA_DOORS`, or ask by name (`DOOR_SWINGS`). A build without the folder, or with `-DREVA_NODOORS` (`driver.py check --nodoors`), has none of them. The proof runs both ways. With the doors, every door and variant, the lab, a two-body set, a scene and the game's seven paths hash byte-identical before and after. Without them, the zombie game, the floor, the lab and the scene still print those same hashes, so nothing the engine does for a game was secretly door code. The two cabinets whose HUD moved were checked by eye.
- The manifest stops sending game modes. **Done 11-02.** `legacy_doors/` and door 11 stay home, and `kit/kit_sync.py` strips every `#ifdef REVA_DOORS` block on the way out. An export from the working tree builds three ways (GL, Vulkan, no display) with no door's name left in it outside two comments, and prints the same hashes. What's still in the engine by choice is the dance floor (`MODE_DANCE`, the crowd's dancing), which is on the rulings list below.
- A written pack spec, `packdump`, golden packs. **Done 11-02.** `design/pack_format.md` is what's on disk, and `anim_bench/packdump.py` holds it to the truth: it shows a pack as text two packs can be diffed in, and `--check` tests every rule. All 404 packs on disk pass, and `--selftest` breaks a real pack 28 ways to prove the checks can fail, with none getting through. The first golden pack is the sample's body, built by code and kept in git.
- RPK2. **First cut built 11-02.** A footer and a directory after the old body, so old bytes never move, and compressed textures as the first chunk (`design/pack_format.md`). `cook_game.py --bc` writes it, with the BC1 and BC3 encoder in `skate_src/author/bc.py`. Vulkan takes the levels straight to the GPU, and GL decodes the top level. The game's folder went from 162 MB of images to 20 MB, and a frame compared with its uncompressed self is 50.6 dB apart at 480 lines, with the same hashes. On the Pi, over the same minute at 480i: it boots in 1 second instead of 3, with 149 MB more memory free, and the frame costs the same (the GPU's render side 72% busy before and 75% after, 37 late and 36 late of about 3,226), so this saved memory and load time and didn't make drawing faster. `packdump --selftest` now breaks an RPK2 pack 40 ways, and none get through. Cooked packs keep every array aligned, which answers UBSan. **Faces, morphs and clips as chunks: built 11-03.** A bake now keeps whatever the old pack had and the new one hasn't, as chunks after the new body, named so a bone can move (`skate_src/rpackchunks.py`, `engine/packchunks.inc`, `design/pack_format.md`). Viktor rebaked from his own glb kept all 20 clips and 5 morphs, and the picture with one playing is 0 of 921,600 pixels off. REVA's film body kept 50 clips and 6 marks and said out loud that it couldn't keep her jaw's weights or the inside of her mouth. Once those tools had run on it, they ran again by themselves at the next bake. Tools now work on a cooked pack and keep its textures. The proof of the reader: all 163 packs on disk mean the same with their clips, morphs and marks moved into chunks, the zombie game and the dance floor print the receipts' own hashes on 77 of them, the engine and Python agree on what 121 packs mean, and packdump caught all 45 ways of breaking one. Not kept: garments put on a finished body with `dresspack.py`. A bake lists them and keeps a copy of the old pack.
- A sample game in aigameengine, which also proves it can start empty. **Built 11-02.** `games/sample/sample.c` is TAG, about 200 lines, the place to start a new game: bodies wander a yard and you run them down against a clock, on the HUD grid, playing itself when nobody's at the pad. Its table leaves out what it doesn't need, and the engine fills the gaps. `make_body.py` builds its one body, so it needs no content and no art tools. `games/sample/play.bat` runs it, and `driver.py check --sample` is its receipt.

**Gate:** aigameengine builds and replays a sample game with no game-mode code in it. **Met on an export, not yet in the kit itself:** a copy of exactly what the kit would get, built with nothing of reva's beside it, on Windows (GL, Vulkan, no display), Linux x86 and the Pi, replays with the same hashes on all three and has no door's name left outside two comments. It played a minute at 240p on the Pi, 0 late of 3,608. The real sync waits for jacob's word.

### Phase 4: any TV
- Safe boot, setup, screen fit, blind recovery, voice prompts. Our own test patterns, written from the 240p Test Suite's manual, because its code is GPL. **Built 11-03** (`engine/display.inc`). A first boot on composite shows 480i and asks, out loud, whether there's a picture. Then it offers 240p with a 15-second automatic revert, fits the four edges, makes a circle round, sets black and white by the TV's own knobs, and offers four test patterns. What it learns is one text file, `save/display.txt`, and a file on the boot partition wins so a person can fix it from a PC with no picture. Holding a button at power-on, or cutting the power twice within 30 seconds of starting, asks again. The screen fit moves the HUD's grid, never the 3D picture, so it can't change what a frame costs. The pause menu has SCREEN SETUP. The voice is 9 short wavs in `voice/`, made by `make_voice.py` and kept in git. A game names the mode it wants (`GameExport.video`) and can change it behind a cut (`E->video`); 240p only happens once the player has kept it on this set. `sample_setup` in the receipt walks every step by tape. On the Pi's real display the tape walked every step, including the live change to 240p and keeping it (3 late of 2,619). Changing mode every 3 seconds for 25 seconds made 8 changes between 480i and 240p with none refused, 14 late of 1,111, under two a change. The console came back after both. Nobody has watched setup on the tube itself yet, so whether it reads well is still jacob's eyes.
- The 50 Hz path (PAL-60 first, then 6 ticks in 5 frames), and idle dimming after 5 minutes. **Dimming built; 50 Hz part built and off.** A pause menu left for 5 minutes goes dark and a touch brings it back, setup's screens dim after 2 minutes, and persistent HUD text now sits at 80% of peak. The clock already does 6 ticks in 5 frames, and setup can reach 576i by a long button hold, but only with `allow50 1` in the file, because nothing has tested it: no PAL set is here, and jacob's Toshiba must never see it. **Not built:** the PAL-60 offer, and rendering 240 lines scaled up to 288 in the 50 Hz modes, which the research says the frame budget needs. A first boot in a PAL country also can't reach this yet without that file, so this needs a PAL volunteer before it's real.
- The reading-size step. **Built 11-03** (`engine/tvtext.inc`, and setup's last screen in `engine/display.inc`). Setup ends with a short line at each of three sizes, and the player picks the smallest they can read, told so out loud. Each line says something different, so a big one can't give a small one away. The pause menu's TEXT SIZE goes straight to that screen, on a PC too. It's one line in `save/display.txt`, `reading small`, `medium` or `large`, and a file with only that line still lets a TV's first boot ask about the TV. The sizes are 35 x 24, 23 x 12 and 17 x 12 cells in the same safe box, on whole pixels, the same shape on the glass at 480i and 240p. A game still places its text on 35 x 24 and doesn't know: its lines land in proportion, and one that doesn't fit drops a size, wraps at its spaces, at a group of them first because that's where a hint's parts meet, or moves out of the way of what's before it, in the order of their rows, keeping the blank rows the game left if there's room. No game needed changing. `E->tvGrid` says what the grid really is, for a game that would rather choose a shorter word, and the sample does. All six games, the pause menu, CONTROLS and every screen of setup were looked at, at the bigger sizes on both TV rasters, and the games in a window too. At the small size none of this runs: every receipt holds, and the zombie game's picture is 0 pixels off the commit before at the `pi` rung on all three rasters and at `high` in a window. The rules don't move at any size: `zombie_read` and `sample_read` in the receipt play at the biggest letters, and their `[simhash]` lines are the plain game's. Nobody's eyes but mine have been on it, and it hasn't been on the tube.

**Gate:** a test across real sets, including a PAL volunteer's set and a flat panel, with a replay in each mode.

### Phase 5: the dev console
- A Pi image, the capped agent, per-unit build flags, dev reload, the basic text terminal, a one-screen chatbox, a small JSON bridge for an AI, and vendor-neutral docs. **Built 11-03,** with the Pi image a script for now.
  - **The JSON bridge** (`engine/bridge.inc`, `club_bridge.c`, `anim_bench/bridge.py`): `REVA_BRIDGE=<port>`, one local TCP port, a line of JSON each way. It can hold the clock, step it, press buttons, take a picture, and read the state, the hash and the dice. A wrong line gets told what a right one looks like. It runs on a fixed step, so a session driven this way replays exactly: its hash at tick 60 is the receipt's.
  - **Per-unit builds and dev reload** (`build_pi.sh dev` and `game`, `REVA_GAMELIB`, Linux): the engine with no game linked, rebuilt only when it changes, and the game as a library that a running engine picks up between ticks. On x86 the engine takes 33 s and the game 0.5 s. It prints the same hashes as a game linked in, and a game that says how big its state is (`stateSize`) carries on where it was. Tested on Linux with no display.
  - **The capped agent and play mode** (`devconsole/agent.sh`, `play.sh`): any AI's tool under a hard memory and CPU ceiling, frozen while the game plays and thawed after.
  - **The text terminal** (`devconsole/watch.py`): one plain line a second over any terminal.
  - **The chatbox** (`devconsole/chat.py`, 11-03): with one TV, a conversation with your own AI in what a composite picture can show, 35 columns by 12 rows of the console's biggest font, inside the safe box and following the player's screen fit. Any command that can answer one message and exit will do, and it runs in the same cap as `agent.sh`. A status row says whether the AI is working and for how long, with the memory free and the temperature. PgUp and PgDn read back. `/play` hands the TV to the game through `play.sh`, freezing the AI, and what it printed comes back into the conversation, with the game's whole log kept for the AI to read. It's its own plain program, so it costs next to nothing and a game that crashes just brings you back to it. Left alone for five minutes it goes dark and any key brings it back, the same as the pause menu. `chat.py --selftest` holds it to all this, on a real pty on Linux, reading the screen back with a small terminal of its own, and ten deliberate breaks each fail it. `/play` went through the real `play.sh` with stand-ins for `sudo` and the engine, a good game and a crash. Not run yet: on the Pi's console, on a TV, or with a real AI tool. The split screen the decisions table offers as another choice isn't built.
  - **Building from the PC** (`anim_bench/pi/cross.sh`, 11-03): the same lines as `build_pi.sh`, run by an ARM compiler in the `revalinux` Docker container, which has the Pi's release of Debian and gcc 14.2. About 45 to 50 seconds a build against about 3 minutes on the Pi. Cross-built, the tower and the zombie game print the receipts' hashes on the Pi, and the tower holds on the tube, 0 late of 597 at both modes. Compiling on the Pi is still what this phase's gate asks of someone with only a Pi.
  - **The Pi image** is a script for now (`devconsole/setup_pi.sh`), and **the docs** are `devconsole/README.md`. ⛔ These scripts parse, and none of them has been run on a fresh card, which is this phase's gate.
- `CLAUDE.md` shrinks toward the 200-line target. **Done 11-03:** 539 lines and 69.6 KB down to 86 lines and 10.6 KB, what comes first, where things are, how to build and check, and the laws. Everything else moved word for word into path-scoped rules in `.claude/rules/`, which load only when their files are touched, and a script checked that every line of the old file is still in one of the new ones, bar four headings that were replaced. Reading a file in `scenes/` was seen to load `film.md` and `old_debts.md`. The research's other suggestion, that a law with no exceptions should be a hook and not prose, isn't done: it's a gap to mention, not this step.

**Gate:** on a fresh 4 GB Pi, an agent builds a small game from the public repo. Replay in play mode with the agent stopped. jacob, 11-03, on using Jake's Pi for it: *"no, because he is using hdmi screens and is busy. this test can come later."*

### Phase 6: the kinds of game
Bench first, then a slice, in rising order of risk: 2D and arcade; character action; crowds and RTS at 400 and up; four-view split screen; a streaming world. This is where the game-facing systems land, each driven by a game that needs it: four pads and action mapping, camera and time controls for games, audio buses and streamed music, UI, stairs and ramps, ray queries and triggers, an animation graph and clip events, particles and decals.

What's built so far (11-03):
- **2D and arcade.** `games/rocks` is the slice, and a second place to start a game: no bodies and no packs (`GameExport.flat`), sprites from one sheet on a 360 x 240 canvas every display agrees on (`engine/sprite.inc`, `E->sprite`, `E->rect`, `E->image`), and its sprites and sounds made by code. A canvas pixel is 2 x 1 at 240p and 2 x 2 at 480i, so nothing scales by a fraction. It's Vulkan's: GL stays for film and the old doors. **Bench:** on the Pi, 8,000 16 x 16 sprites hold with 0 late at 240p and at 480i, about 4.8 ms of CPU a frame (`anim_bench/pi/sprite_bench.c`, 15 seconds of each). That's more than a game on this canvas will ask for. The game's own minute at 240p: 0 late of 3,607, about 1.2 ms of CPU.
- **Four pads.** SDL reads them on every machine now, the Pi's direct road included, in place of the old `/dev/input/js0` code that only knew one pad and three buttons: its table knows nearly any pad's layout. Pad 0 is also the keyboard. A pad that comes back takes the lowest free seat, and any player's Start opens the menu. A tape carries all four (v3; older tapes read as those three not being plugged in), and so does the bridge. Tested on the Pi with virtual F310s through `/dev/uinput`, the road a USB pad takes: three players, one pulled and plugged back into its seat, and that tape replayed on Windows with every hash the same. A minute with no pads is still 0 late, so reading them costs nothing idle. ⛔ Opening a pad stalls for up to 70 ms, so plugging one in mid-game costs a late frame or two. **Not tried:** real second, third and fourth pads.
- **Action mapping.** `E->action("fire", PAD_A)` at boot gives a handle, `E->actionHeld` and `E->actionPressed` ask by player, and `save/controls.txt` moves them (`fire rb`, `p2 fire a x`). A tape carries the bindings it was recorded with, because the same thumbs on another binding are another session. The pause menu's CONTROLS screen changes them; driven through the bridge, moving fire to X wrote the file, and after a restart A no longer fired and X did.
- **Character action.** `games/tower` is the slice, and the place to start a 3D game: everything in it is made by code. On the Pi it's 0 late of 3,607 at 240p and 0 late of 3,597 at 480i, about 2.4 ms of CPU a frame, with the whole minute being the game playing itself.
  - **The walked world** (`engine/spans.inc`). A set line `climb` says its surfaces are walked on. Every prop's real triangles go into columns of solid spans over the floor plan, the way Recast does it, so ramps, stairs, a bridge you can walk under, an upper floor and a wall are all the same data with nothing to author but the geometry. `E->walk` slides a body along what stops it, `E->ray` walks the same columns, and triggers and marks come from the set. The hero gets gravity, jumping and eased steps from the engine; the level's 2,000 triangles build in 51 ms on the Pi. A set without the line doesn't pay for it and doesn't change, so film and the zombie game's street are as they were.
  - **The lens and the clock.** `E->lens`, `E->lensFree`, `E->shake`, `E->timeScale`, and `E->tune->stickYaw` so a stick pushes the way a turned lens is looking. The tower's lens is on an arm the right stick turns, pulled in by a ray when a wall gets in the way, and a punch that lands stops time for a moment.
  - **The animation graph** (`engine/animgraph.inc`). A text file beside the game, read again when it changes: states, transitions on flags and values, and events at a point in a clip that reach the game through `GameExport.clipEvent`. The punch's hit lands on an event. It keeps its own exact place in the clip, so an event falls on the same tick at any frame rate. **On every body since 11-03:** `E->bodyGraph(i, file)` gives a body its own run of a graph, and `E->bodyAnimSet`, `E->bodyAnimIn` and `clipEvent` work as they do for the hero. Each body keeps its own place, in the tick and whether or not it's on screen, and stops while it's hidden. Its clips can be its own, or borrowed by name from a bank if its rig can take them: citizens played the zombie bank's walk and idle this way, seen in a picture and with the packet parting between two clips. A change of state crossfades. A body with no graph picks its clips the old way, by `bodyLoco` and the crowd's ladder, so the zombie game and the crowd are as they were. In `games/tower`, the other players play the hero's own file, so their punches land on its event, and the things chasing them have `chaser.anim`, whose grab lands on an event and not on being near. The rewrite changed nothing for the hero: last step's tower, built on the new code, prints last step's hashes. `anim_bench/graphcheck.py` is 27 checks through the same table any game gets, comparing events tick by tick at odd tick patterns, and 9 deliberate breaks each fail it.
- **Split screen.** `E->views` and `E->viewLens`: the packet carries up to four lenses and rectangles, and the renderer draws the same frame's data once per view. Two are stacked, three or four are in quarters, each with the picture's true shape. In the tower, any other pad joins by pressing A and the screen splits; a tape with players joining and leaving is part of its receipt. On the Pi with the real renderer, the tower's level and 100 of its light figure (about 300 triangles each) hold at one, two and four views, 0 late of about 897 at both 240p and 480i, 15 seconds of each, at 4 to 6 ms of CPU. That's the light figure: Phase 0's lab, with heavier bodies, held four views at 240p and had 86 late of 599 at 480i, so a game's bodies decide this one. **Since 11-04 (queue step 11):** particles face each picture's own lens. `E->tvView` in hud and `E->uiView` on a menu put text in one player's picture: it's placed on the same 35 x 24 as anything else, and the engine fits it to that picture's part of the grid, the way it does for a reading size. The cells stay the same size, so two pictures have 12 rows each and four have 17 columns by 12 rows. In the tower, each player has their own numbers. ⛔ At the largest reading size with four pictures, each picture only has 8 columns by 6 rows, so a busy screen overlaps there, and it says so.
- **Crowds.** `games/herd` is the slice: hundreds of bodies on a field, led about by a cursor, and the place to start an RTS. `E->nearby` answers who's close from a grid over the floor plan, in the same order walking the whole list would, so the zombie game's hashes didn't move when it changed over. `E->tune->bodyCap` says how many may be drawn. **Bench**, 15 seconds of each, every body in the picture and the draw count printed, on a light figure of about 300 triangles and 26 bones:

  | Bodies drawn | 240p | 480i | CPU a frame |
  |---|---|---|---|
  | 368 | 0 late of 897 | 0 late of 896 | 7.5 ms |
  | 528 | 0 late of 896 | 0 late of 896 | 10.4 ms |
  | 684 | 2 late of 895 | 0 late of 896 | 13.2 ms |

  So 400 and up is real, for a body that's budgeted for it. The zombie game's bodies cost about six times as much to pose: 100 of them hold at 240p (0 late of 863) and have 48 late of 815 at 480i, 200 run at 51 fps and 41, and 400 at 37 fps and 32. A crowd is budgeted by its body's weight. Herd's own 800 bodies simulate with 0 late, with about 100 of them in view.
- **A late frame isn't a debt.** On the tube the clock pays one tick a flip now, which is what this plan's schedule always said, and two at most at 50 Hz. Before, a frame a little over budget owed the next one two ticks, and each tick costs a whole pass, so 18 ms of work became 73 and 400 bodies ran at 13.7 fps. Now the game slows by a hair and each frame costs what it costs, as a console does. Being over budget is still over budget: at 37 fps the game runs at 37/60 speed.
- **A streaming world.** `games/roam` is the slice. A set line `cell` is a prop that's only in memory near the hero (`engine/stream.inc`). A thread reads the file, so the disk can't reach a frame; the main thread puts in at most one cell a tick; a texture's copy goes in the next frame's commands with nothing waiting for the GPU; the thread builds the cell's own collision, so it can be walked on; and the renderer holds what's evicted until no frame in flight can be using it. Anything measured or compared does it all on the spot, so the receipt doesn't depend on a disk's speed.

  | A minute at 240p, cache dropped first | Late frames | Cells late | Average to put one in |
  |---|---|---|---|
  | 1,600 cells, 37 KB each | 0 of 2,998 | 0 | 0.72 ms |
  | 1,600 cells, 80 KB, 256-pixel BC textures | 0 of 2,998 | 0 | 0.80 ms |
  | 576 cells, 351 KB, 512-pixel BC, 200 boxes | 0 of 2,987 | 3 | 1.51 ms |
  | The same at three times her speed | 0 of 2,988 | 10 | 0.79 ms |

  In that last one the stick stalled for 297 ms and nothing noticed. The heavy world at 480i has 103 late of 2,878, at 2.4 ms of CPU at worst, so that's the picture's pixel budget and not the stream.

  **Far detail and bodies that stream, since 11-04 (queue step 12).** A set line `fardetail <stem> <distance>` names one pack with a small mesh for each cell, named after it, in the world's own coordinates. It stays in memory, and a cell that isn't here shows that instead, so the horizon isn't empty and a cell doesn't appear from nothing. They're only in the picture: no shadows, and nothing solid. `games/roam` keeps each cell's ground and its six tallest boxes. For people, `E->bodyPack` asks for a body's pack by name while the game runs, from `bodies/` and then `packs/`, `E->bodyPackReady` says when it's in, `E->bodyWear` puts it on a body, and `E->bodyPackDone` lets it go once no body has it on, so a game can have more kinds of people than fit in memory. It's read on the stream's thread, at most one goes in a tick, and a run being measured does it on the spot. **Not built:** a far mesh per cell tops out at 4,096 cells a pack, and a stand-in doesn't fade into the real cell, it swaps. The hero can't change packs this way.
- **Audio buses and streamed music** (`club_audio.c`). Every voice is on a bus whose gain can fade, and music is streamed off the disk by a thread of its own into a two-second ring, so the disk can't reach the sound or a frame. Ogg Vorbis (`third_party/stb_vorbis.c`, public domain, kept in the repo) or 16-bit WAV, at any rate. The tower's mix dump has its music in every tenth of a second. Nobody has listened to it: that's jacob's ears.
- **UI** (`engine/ui.inc`). `E->uiMove` in tick and `E->uiList` in hud: a list on the TV's grid. The d-pad works as the stick everywhere. **A fuller toolkit since 11-04 (queue step 10):** a menu said in tick, every tick, from `E->uiBegin` to `E->uiEnd`, with buttons, toggles, sliders, word choices, tabs on LB and RB, words typed with the pad on a keyboard on the grid, rows that are switched off, and a long menu that scrolls to the row in use. Each row answers there and then, the values are the game's, and where the menu is (`EngineUi`) is in the game's state, so a replay does the same. It draws itself with the HUD on a dark panel, so it reads over any picture. A menu is one player's or anyone's, in any part of the grid, and two can be up at once. At a bigger reading size a label and its value stay on one line if they fit, and the panel goes round where the rows land. `games/tower` has one when it's over, and `anim_bench/uicheck.py` is its test. **Not built:** there's no pointer, because it's made for pads, and a word can't be typed on a real keyboard.
- **Particles and decals** (`engine/gamefx.inc`). `E->particles`, `E->decal`: two fixed pools where the oldest makes way, through the renderer's cheap effect path. They roll their own dice, so they can't change the game.
- **Objects a game makes and takes away** (`engine/objects.inc`, 11-03). `E->prop` names a model, `E->spawn` puts one in the world and gives a handle, `E->place` moves it, `E->paint` colours it, `E->despawn` takes it out, and `E->object` says what it is now. 1024 at once, and a handle is never given out twice, so a stale one is safe: calls on it do nothing. They draw the way a set's props do, with shadows and culling, and copies merge into one call. `OBJ_SOLID` makes one stop bodies and rays: by its real triangles on a `climb` set, so it can be stood on, and as a circle on any other set. Moving a solid one along costs next to nothing, because its columns go with it, and it carries whoever's standing on it. `E->bodyNew` and `E->bodyGone` wake and put back bodies from the pool. It's all in `[simhash]`, and a run that never makes one mixes nothing, so no other game's receipt moved. `games/tower` uses all of it: crates you can jump on, beacons that change colour, and a lift. With nobody at the pad, its hero walks the same path to the last digit as before the crates were there. `anim_bench/objcheck.py` is 102 checks through the same table any game gets, and 12 deliberate breaks of the engine each fail it. It also found two older bugs in the walked world, both fixed. A ray further than about (W+H+4) x 0.2 units from a small grid went straight through it, which would have hit streamed cells too. And a body falling beside a wall could then walk through the wall, because anything in its circle let it go anywhere. Now it can walk along what's touching it or out of it and never further in, grid by grid, and a move longer than a cell is walked a cell at a time, because a surface is one cell thick and a tall box is hollow. A move past 2 units in a tick is a game putting someone somewhere, and isn't walked. **Since 11-04,** `E->propDone` lets go of one (see "A long session"). `setLoad` clears the rest, and nothing can change level mid-game to test that until step 16.
- **Physics a game can reach** (`engine/objects.inc`, `engine/ragdoll.inc`, queue step 9, 11-04). `OBJ_LOOSE` on anything from `E->spawn`: it falls at real gravity, bounces less each time, slides to a stop in the time friction says, stays put on a slope its grip can hold, rolls if it's `OBJ_ROUND`, and comes to rest. It lands on what's really there, because it uses the walked world's surfaces: a crate lands on a deck, stops at a wall and slides off an edge. A tall thing pushed high up goes over onto its side, and a nudge at its foot leaves it standing. Loose things stack and knock each other along, and knocking the bottom one out brings the stack down. `OBJ_RESTING` starts one asleep where it's put, in the air if you like, until what's under it changes or something hits it. A solid one can be stood on while it's asleep, and a body walking into one that's awake shoves it along. `E->push` and `E->pushAt` move one, `E->feel` sets its bounce and grip, `E->objectOnRay` says what a straight line meets, and `GameExport.objectHit` says what hit what and how hard, once a touch, and a game can take either away inside it. Any body goes limp with `E->bodyLimp`, and -1 is the hero: the ragdoll film uses, on what's really there, from whatever pose it's in, and `E->bodyRise` puts it back on its clips. 16 others at once. `games/tower` uses all of it: a punch sends a crate flying, a crate going fast enough knocks a chaser down, a chaser that's knocked down falls limp and is gone 3 seconds later, and whoever's caught goes down. Loose things are all in `[simhash]`, and a limp body is what's drawn and not the rules: with and without one, `[simhash]` is the same and `[packet]` parts. `anim_bench/physcheck.py` is 134 checks through the same table any game gets, on a `climb` set and on no set, comparing every hit's tick at odd tick patterns and measuring each fall from the ragdoll's own log, and the five deliberate breaks tried each fail it. Each experiment has been looked at on Vulkan too. A game's bodies showed three things wrong in the ragdoll, fixed for them and left alone for film, whose takes were made on the old ones: friction read how deep a joint was after the floor had already put it right, so there was next to none; two rules split a correction evenly between a heavy joint and a light one, which pushes the whole body along, so a body crawled along its own length on the floor at 5 units a second; and a body standing with straight legs kicked its feet half a metre up as it went down. And an old rule banned any body whose drawn hips were more than 4 units from its place on the floor, so a body thrown that far was lost to the pool for good, which was the one way a limp body reached the rules: found by a deliberate break, and a game's limp bodies are exempt now. **Not built:** this is one point with an orientation, not a solver of corners, so a box doesn't rock and a stack doesn't lean before it goes. There are no joints or hinges between objects, and no vehicles. A limp body off screen doesn't fall until it's seen, which is why no rule may wait on `E->bodyLimpState`.
- **What a bigger game needs** (`engine/gameworld.inc`, queue step 16, 11-04).
  - **Changing level.** `E->level` loads another set while the game runs, behind a fade if it's asked for one. What the old level had goes: what the game spawned, the props, and a few ticks on the memory of any pack nothing's using now. `GameExport.levelIn` is where a game puts things in the new one, with the screen dark. Tried by going between two levels 1,682 times as fast as they'd go: the process stayed at 278 MB and geometry at 0.3 MB. **Not built:** the load itself happens between two frames, so it's a cut and not seamless. A game that wants no cut has `cell`.
  - **Finding a way.** `E->path` gives points to walk to in a straight line. On a set that says `climb` the first call on a level makes a map of where a person can stand and step: a point every half a unit on each surface, and each link is the real `E->walk` itself in short steps, so what the map says is what a body will do. After that a search only reads the map, so it's quick at any distance. On the tower it's 35,000 places, made in 257 ms, and the way from the floor to the top deck looked at 9,900 of them in 1.5 ms and came out as 6 points: across the floor, up the ramp, up the stairs. A body walked them with `E->walk` and got there. What's been spawned since the map was made is asked about on the way. If there's no way it gives the way to as near as it could get. On a set without `climb` it's the nav grid actors use. **Not built:** a streaming world, or a walker a lot wider or narrower than a person, has no map and searches the columns live, with a budget. It can't jump a gap or use a lift, and the walker is the game's.
  - **Props on any bone.** `E->attach` puts a model on a bone by its name in that pack or by a plain word (head, hand_l and so on), and it goes with the bone wherever clips, a face or a ragdoll take it. The offset and scale are for a body of ordinary height, so the same crown fits a child. 256 at once, on and off whenever the game says, with nothing added to the pack, and a conversation file can do it with `attach` and `detach`. They're what's drawn, not the rules, and they cast shadows.
  - **A music clock.** `E->musicClock` after `musicPlay`: beats a minute, where the first beat is, beats to a bar. `E->musicBeat` says where it is, and `GameExport.beat` falls on the tick each beat comes round. In a live game it follows the sound itself, because a late frame slows the game by a hair and the sound doesn't stop for it, and a tape keeps what it read, so a replay's the same session. In anything measured it's the tick clock and no sound's involved. At 120 beats with the first at 0.25 s, beat 16 fell at 8.25 s to the tick, on the recording and on its replay. **Not built:** nobody has played to it with the sound on: that's jacob's ears.
- **People built while a game runs** (`engine/people.inc`, queue step 15, 11-04). jacob, on players making their own character: *"thats a per game question / we need the capability and the feature."* The people builder was a Python tool, so a game couldn't call it. `E->bodyBuild` is one in plain C, from the same measurements and tables: a body is a stature, how many heads tall it stands (5.5 at five years old, 7 at twelve, 7.5 grown up, and the whole body's proportions follow), how it's built, shoulders, waist, hips, limbs, legs, bust, belly, nose and jaw. It has skin, eyes, three hairstyles and a bob with their colours, and sleeves and shorts, trousers or a skirt. It writes an ordinary pack into memory on the stream's thread and the ordinary loader reads it, so a built body is the same as any other: it stands, walks and runs by itself, takes clips from a bank, is lit and casts shadows, and its face does everything step 14's can, with a jaw, eyes on bones, lids, a smile, brows, a mouth that opens and teeth inside it. The same spec builds the same bytes on any machine, and it's about 2,500 triangles and one small texture. `E->heroWear` puts one on the hero, which nothing could do before. `E->tune->heroSpd` below 0 now means the stick doesn't move her, because a menu needs the stick. `games/people` is the slice, and the place to start a game whose players make their own person: a menu that changes a person live while a street of people dealt from the dice stands behind, none of them made ahead of time. What it writes passes `packdump.py --check`. **Not built:** this isn't film's person: the hair's a shell and flat panels, the face is painted, and hands are mittens with a thumb. There are no separate garments, so clothes are the skin's own colour, with nothing that hangs. A built body only has its three clips of its own, though a bank's work on it. And how it looks is jacob's to judge.
- **Faces and conversations** (`engine/face.inc`, queue step 14, 11-04). Any body, and the hero, can blink, breathe, say a line, look at something, and take a shape by name. `E->bodySay` plays a sound on the voice bus and its own loudness moves the mouth and the head, and if there are words the shape of them opens and closes the jaw as well. With no sound, the mouth moves on the words alone for as long as they take to read, so a game has talking people before it has a single recording. `E->bodySayData` and `E->bodyMouth` feed a mouth from a game's own data, for a song's lip sync. `E->bodyLook` and `E->bodyLookBody` turn the head and eyes, the eyes first, with a neck's limits. `E->bodyShape` holds any shape. Each body has its own face, even on a pack others are wearing, because its moved positions go in the frame's packet. It's all drawn and not the rules: with a face on the hero, `[simhash]` hashes the pose from before it went on. **A conversation is a text file,** `talk/<name>.talk`, read each time it starts: who says what, the lens, who looks at whom, shapes, a value for a body's animation graph, a beat, wait for a button, a question put to the player with its answers going to labels, an event for the game, and A cuts a line short. Left to itself the lens opens on both of them, goes over a shoulder onto whoever's speaking, and cuts in close every third change, always on one side of the line between them and never through a wall. The words go up on the TV's grid and fit any reading size. When it's over the lens goes back to what the game had. `games/talk` is the slice, and the place to start a game whose people talk, with its bodies, room and voices made by code. A voice is read and its loudness worked out on the stream's thread, and it's let go after a minute with nobody using it. **On Vulkan** this brought the mouth's opening across (a hole cut between the lips, drawn by its own two pipelines, so nothing else's shaders changed by a byte) along with the jaw on a bone and shapes per body. On film's current Reva the mouth opens centred and dark. `E->heroFace` is new too, because a game had no way to turn the hero. ⛔ `v1_r2moves` opens to one side: its lip marks and its teeth don't line up with each other, which is that pack's and not the engine's. **Not built:** a seated body's shot sits too high, because the lens uses how high its eyes are at rest. Split screen doesn't get its own conversation lens. The mouth's shape is how far it's open, not which sound it's making. And nobody has listened to it: that's jacob's ears.
- **A long session** (queue step 13, 11-04). The engine can run for as long as anyone leaves it. Bodies of different sizes coming and going used to fill the renderer's list of freed memory in two minutes, then what was freed was lost for good, and the memory grew until the engine exited. Now a freed piece joins the free pieces either side of it, and a buffer that grows gives back what it had. A buffer's or a texture's name comes back when it's deleted, so the renderer's tables stop at about the size the game needs. A place in the set's table comes back too. A game can let go of a model with `E->propDone`: its memory goes, its number still works, and the next spawn reads it again. A model the set was using first stays, and so does one with anything out. And a pack that comes back says nothing about itself the second time, because that's what filled a long session's log. `REVA_MEMSAY=<ticks>` prints a `[mem]` line, so a long session can be read. **Tried:** 16 kinds of body changing on four bodies at 10 changes a game second, run as fast as the machine would go. Before: 50 to 213 MB of geometry in under two minutes, and a 41,000-long name table. After: one block, about 53 MB, 2 free pieces, and the tables flat. **Not built:** a game's own images and sound bank aren't let go, and the cells a level loaded stay until step 16.
- **A game's state is in the receipt** when it gives `stateSize`. A flat game has no hero or bodies, so its `[simhash]` didn't move at all before this.

**Gate:** each kind's replay. An explicit go or no-go on big open worlds, on SD and on SSD. This was the highest risk in the plan, because nothing about streaming had been measured.

**Big open worlds: go.** This Pi boots from a USB stick that reads 28.7 MB/s, slower than most SD cards are specified for, and the fastest run above needs about 2.7 MB/s. So the disk isn't what limits a big world on this hardware: the picture's budget is, as it is everywhere else. ⛔ That's measured on this stick only. Neither SD nor SSD has been measured, because neither is here, and jacob, 11-03: *"we are working with what we have."* Far detail, bodies that stream and a long session have all been built since (queue steps 12 and 13, above).

### Phase 7: the PC picture
This is the graphics list: everything a strong PC should show that the engine can't do today. It's visuals only, with the same game underneath, so the same tape gives the same hash and nothing is made that the Pi can't play.

**It can start as soon as Phase 2's one body path and forward pass exist, and run alongside Phases 3 to 6**, because it doesn't depend on them. It shouldn't start sooner: today every feature would be written three times, once per body path, and that repeat work is what the 09-01 audit warned about. This replaces the engine half of `lighting_rung.md`.

What the PC offers, measured 11-02 (`anim_bench/pi/gl_probe.c`): a GTX 1070 through ANGLE on D3D11 reports OpenGL ES 3.1 with compute, 16-bit float targets, 8x MSAA, 8 render targets, anisotropic filtering, BC1-3 and BC7 textures, 16K textures and GPU timers. It runs Vulkan 1.4 directly, and lists ray tracing. That makes it a mid setting: its driver line is frozen, so a newer GPU is needed to prove the top one.

"Today" was checked against `crowd_kms.c` on 11-02. The Pi column is a first guess from what it advertises and from today's bench, and none of it has been timed.

**Where it stands (11-03).** Steps 1 to 3 are built on the new renderer, and steps 4 to 7 aren't started. Each feature is a setting in one table (`engine/gfxset.inc`), a row per rung. `save/graphics.txt` moves any of it and `REVA_GFX="msaa=1,hdr=0"` overrides both, which is each feature's off-switch, and what a machine can't do comes back off. Nothing moves by itself. At the `pi` rung everything's off: a frame of the zombie game differs from the commit before by 0 of 921,600 pixels, and no receipt moved at any point.

- **Step 1.** `aniso`. `msaa`: the world draws to its own target and the HUD goes on after. BC7: `cook_game.py --bc --bc7` is a PC's cook, because the Pi can't sample it, and anything that can't decodes the top level at load. On a test frame BC7 is 49.5 dB against BC1's 40.2, at twice the size.
- **Step 2.** `linear` is 1 for the shader alone, or 2 for sRGB textures with the light in the target. `hdr` is a 16-bit float target and a pass to the glass. `tonemap` is Stephen Hill's fit of the ACES curve, with `exposure <n>` in a set and a fixed curve from the day clock, never measured from the picture. `bloom` is from what's over white. `grade` is a LUT: `grade <name>` in a set reads `grades/<name>.cube`. The curve's base is set so the picture's average brightness stays near the Pi's: 70.4 against 67.7 on the street, 46.1 against 56.0 in the tower.
- **Step 3.** `hemi`, with `ground r g b` in a set. `cascades`: four tiles of one map, gathering casters further. `lights`: clustered forward, up to 256, from `light` and `spot` lines in a set, and from a game for one frame through `E->light` and `E->spot`. `tiles`: an atlas for lights' own shadows, from a `spot` line's last number or `E->lightShadow`, where a spot takes one of 16 tiles and a lamp six. `ao` and `contact` come from the depth image, at half size. `probes`: `REVA_BAKE=<file>` in the no-display build traces a grid over a set's real triangles and exits, 63 x 10 x 63 in 1.2 s for the tower, and a set picks up `sets/<name>.probes`. `volumetric`: shafts through `haze <density>` in a set, marched through the sun's shadow map.

**The first readings on the Pi,** once each, reported and not chased. The tower, 15 seconds of each and at the `pi` rung otherwise:

| Setting | 240p | 480i |
|---|---|---|
| None, a minute | 0 late of 3,306 | 0 late of 3,597 |
| `aniso=4` | 0 of 597 | 0 of 596 |
| `msaa=4` (it has no 2x) | 0 of 596 | 1 of 596 |
| `linear=1` | 0 of 596 | 1 of 596 |
| `hemi=1` | 0 of 596 | 0 of 597 |
| `probes=1` | 0 of 596 | 3 of 594 |
| `lights=6`, all six reaching most of the picture | 39.8 fps | 16.9 fps |

The tower is a light scene, so those say what each costs on top of very little. Six lights per pixel don't fit, as Phase 0's table warned.

**The toon pass, since 11-04 (queue step 17).** A pack with a `ToonStyle` node is drawn in the show's look on Vulkan: two tones cut from one key light into a cool shadow, gentler on a dark colour, a rim along the silhouette, a body keyed from above the lens with the room's shadows reaching it as a dim, and a `ToonSet` prop lit as a room's lit, from its ceiling. The ink line is a second draw of the same copies, its back faces pushed out on screen, and `~face~` meshes don't get one. A `Glow` prop is unlit at its own colour. The palette's hand-tuned as it's stored, so toon pixels are worked out there on every machine, and at the HDR rungs they say so in their alpha and the post pass leaves them alone: no tonemap, no exposure, no grade, no occlusion. All of it's in shader variants, so the plain shaders didn't change by a byte. Looked at on the New Mayflower's bridge with Viktor and Wanda talking, at `pi` and `high`. The mirrors and the glossy floor came across in step 18.

**Step 18, film and the old game modes onto Vulkan (11-04, in progress).** Written and built on Vulkan: deep space and its named stars; the set's beams; the captions in their real typeface, with their box; the practical lights a scene puts in someone's hand and a scene's `lamps=`; the glossy floor, with everything above it reflected and fading with its depth; the set's wall mirrors, with a prop's reflected copy culled the right way and its ink line the right way round, a `MirrorOnly` prop seen only in reflections, and a sheer prop; smoke as soft sprites; the pool's water reading the sky; the half-pipes; the old park and its rails; Wan-To's star with its cut-away, corona, loops and web; the lab's multi-camera wall as views of one frame; the face marks and the lead; a scene's scrim; and film's own way through the post pass, on an 8-bit target on its own colours: the knee on brightness so a bright colour keeps its colour, bloom from what's saturated, the grade, the vignette, the fisheye and the phone's frame. The set's timeline (`track`) now plays before any pass, so the sun's pass and the picture agree, and a mover stretched tall isn't culled while it's on screen. `mirrors` in `gfxset.inc` is the off-switch (`REVA_NOMIRROR=1` still works), and the copies a mirror asks for are left out of the packet when it's off, so the receipts can't tell. **Looked at:** film's test take (`_pdoom_test`, 12 s at 30, `--film`) shot on Vulkan beside film's reference at 1, 3, 5, 8 and 11 s: the staging, the bodies, the movers, the beams, the captions, the floor's reflection and the colours read the same. **Not yet looked at:** a mirror room, the pool, a half-pipe, smoke, the star, the phone's frame and the wall, which that take doesn't show. **Left to do:** the lighthouse door (its passes are written, `legacy_doors/gfx_lh.inc` and `shaders/lh_*`, not yet applied), every build line onto Vulkan (`driver.py build`, `build_linux.sh`, `build_pi.sh`, `skate_src/band/build_exe.sh`), taking the old renderer's code out, the write-up and the tick.

**Not done:**
- The gate's A/B per feature on a toon body hasn't been run. The toon pass itself was looked at at `pi` and `high`.
- Film reaches the new picture since step 18, on its own colours. The HDR road's tonemap, occlusion and LUT grade are a setting away for it and haven't been tried on a take.
- jacob's eyes haven't seen any of it, and they judge the look.
- Anisotropic filtering hasn't been looked at from a low lens, where it shows.
- Baked probes still look uneven near walls in the tower.
- Shafts come from the sun only, not from spots.
- With cascades on, a PC's packet has more sun draws than the no-display build's, because casters are gathered further. The rules don't change, and the `pi` rung's packet doesn't.

**Step 1: the foundations.** Cheap, and everything after them leans on them.

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Vulkan shaders | ES2-style GLSL on the old path | GLSL to SPIR-V on the new renderer, with one source per material class | Yes, in Phase 2 |
| Real depth images | The sun map packs depth into an 8-bit colour target through its own pass | Depth images and hardware shadow comparison | Yes |
| Anisotropic filtering | Never set | Per texture, level by setting | Test |
| Mipmaps on every texture | Non-power-of-two textures get none | Vulkan has no such limit | Yes |
| Compressed textures | None | BC1 and BC3, which both machines sample through Vulkan, plus BC7 on PC | Yes |
| MSAA | None in the scene | 4x to 8x by setting | Test, up to 4x |

**Step 2: colour.** The outside review's first step, with linear colour put under it, because a tonemap on gamma-space light is wrong.

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Linear lighting | None: all lighting runs on gamma-encoded values | sRGB texture decode, linear maths, sRGB output | Test, in the shader only; sRGB render targets double tile memory |
| HDR | 8-bit targets, light clips at 1.0 | 16-bit float target | No, it doubles tile memory |
| Filmic tonemap and exposure | A soft knee at 0.70 | An ACES-style curve, with exposure set by scene and time of day | The knee stays |
| HDR bloom | Bloom on 8-bit | Threshold in HDR | Cheap version or off |
| Colour grade | `grade=`, film only | A LUT by scene | Maybe |

⛔ The toon look stays out of this. The show's palette is hand-tuned against a light capped at 1, so a tonemap has to stand off toon bodies or it repaints the show.

**Step 3: light and shadow.**

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Ambient | One flat sky colour; a hemisphere only in toon rooms | A hemisphere everywhere, then baked probes | Hemisphere probably yes |
| Ambient occlusion | None | Screen-space from the depth texture | No |
| Baked light | None | Lightmaps or probes baked by a tool at the cook | Yes: cheap at run time, the one upgrade that helps both ends |
| Sun shadows | One map, +-28 units, 2048 or 4096, four taps, no cascades | 3 or 4 cascades, softer filtering by setting | One map, if it fits the budget |
| Spot lights | No such type: point lights only | Spot lights | Budgeted |
| Point and spot shadows | None | A shadow atlas | No |
| More lights | 6 point lights | Clustered forward | 6 or fewer |
| Contact shadows | None | Screen-space | No |
| Volumetrics | Fog and glowing beams only | Volumetric fog and light shafts | No |

**Step 4: materials.** After light, because GGX needs data no pack has.

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Specular, roughness, metal | None; water has Fresnel, floors have `gloss` | GGX | A simple specular, maybe |
| Material data | The bake keeps only base colour, and UE exports lose their materials | New pack chunks, appended and never rebaked, with a table by material name as the fallback | Same data, simpler shader |
| Reflections | Planar mirrors and glossy floors only, no cube maps | Reflection probes, then screen-space reflections on top settings | Probes maybe |
| Surface detail | Normal maps | Detail maps, parallax by setting | Normal maps as budgeted |

**Step 5: characters.** Bodies and faces are this engine's strength, so close-ups should show it. Realistic bodies only; the toon pass stays as it is.

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Skin | No subsurface scattering | Pre-integrated or screen-space | No |
| Hair | Alpha cards on chains, no hair highlight | An anisotropic highlight | Maybe |
| Eyes | They aim and blink, with no wet reflection | A cornea highlight and reflection | Maybe |
| Cloth | No sheen | A sheen term | No |

**Step 6: effects and world.** Games need budgeted versions of the first two in Phase 6, and these are the PC's richer ones.

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| Particles | Smoke wisps and line tracers only | A general system, with soft particles on PC | Budgeted |
| Decals | None | Projected decals | Budgeted |
| Level of detail | Almost none | Longer distances, film meshes as top detail, impostors far off | Shorter distances |
| Water | Reflection and Fresnel, no refraction | Refraction and shoreline | As it is |
| Foliage | None | Instanced grass and trees | Sparse |

**Step 7: the display.**

| Feature | Today | Build | On the Pi |
|---|---|---|---|
| 100 and 144 Hz | None | Interpolation from the packet | 60 locked |
| Resolution and widescreen | The aspect follows the window | Static settings, with a rule that keeps the wider view fair in versus play | 4:3 |
| Flat-panel look | None | A measured tube model, as an option | The real thing |
| Depth of field | None | For film and cutscenes | No |
| Motion blur and TAA | None | Available, off by default in games: jacob gets headaches from blur on flat panels | No |
| HDR display output | None | Later, optional | No |

**Gate:** the same tape gives the same hash as the Pi, and Pi replays are unchanged. Each feature has an off-switch and an A/B on a realistic body and a toon one. Settings are static on the PC too, with no automatic cuts. The GTX 1070 holds 2560x1440 at 100 Hz on its setting, and jacob's eyes on the PC monitor judge the look.

**Measured, not looked into (11-03):** on Vulkan in a 1280x720 window on the PC, the `[frames]` counter calls about one frame in five late in all four games (258 of 1,201 in herd), at about 1 ms of work, with a worst gap near 34 ms and none late by two fields. That may be 60 images a second against the 100 Hz panel, but the cause isn't checked, and this gate will have to answer it.

### Phase 8: public and online
Licence, notices, pinned toolchain, fuzzed parsers, store packaging, a Pi image. Rollback netcode comes later, on the determinism already built.

**Gate:** a release build replays, and Pi and PC hashes match across a session.

## Work that waits for a number

These are triggered by a measured need, not scheduled: threads, moving to SDL3, field rendering, pose buckets at proof scale, a visual editor, compute on the PC, ray tracing, which would need a new backend, and a look at cooling, if a replay ever reports throttling (`pi_run.sh` prints the temperature and the throttle flags on every run). Text files, live reload and headless checks are the first answer to "editor", in keeping with AI first.

## Two engines to learn from

jacob pointed at both on 11-02. Neither can be the base: raylib is OpenGL only, and its FAQ says there are "no plans to support any other graphics APIs", and Wicked assumes bindless textures with no fallback, which the Pi's Vulkan driver doesn't have. Both licenses let us copy code as long as its notice stays (raylib zlib, Wicked MIT).

- **[raylib](https://github.com/raysan5/raylib)** (plain C99, and its Pi road is DRM/KMS like ours) is a parts bin for the gaps. `raudio` for streamed music, which builds standalone on miniaudio (Phase 6). `raygui` for UI and settings, one header with a standalone mode (Phases 4 and 6). Its evdev pad reading for four players (Phase 6). `raylib.h` and its 220+ examples are a model for `engine.h` and the public samples (Phases 3 and 8).
- **[Wicked Engine](https://wickedengine.net/)** (C++17) is the reference for Phase 7: its features match that list closely, each readable in one codebase. It targets Vulkan 1.3 too, and its one device interface over three backends is a model for our thin layer. Read its job system, GUI, loading screen, in-game console and Jolt physics before building ours in Phase 6. Its VRM import is a possible route to new bodies from VRoid Studio.

## What we said no to

Automatic quality cuts of any kind. A script language. Overclocking. AI inside the games. Tuning to one TV. A big-bang rewrite. An ECS library, fibers, and SDL3 GPU, sokol, bgfx or the Vulkan Memory Allocator in place of our own thin layer. Virgo as the engine's renderer. Reading files on the frame thread in a shipped game. PAL modes on the NTSC test set.

## Rulings jacob still owes

Each has a working default, so nothing is blocked except where it says so. Gone over with jacob on 11-03.

1. **HDMI and composite at once: parked.** jacob: *"i dont remember how we did it on the pi, and frankly its not important right now because we only have the one conection."* Build for one connection and SSH for now. The two-screen dev setup comes back when a second screen is in play, and the Pi docs and the only config measured say the two are exclusive, so it needs finding again then.
2. **"Same bodies".** Ruled, jacob 11-03: *"sounds good."* Same skeleton, clips, collision and count on every machine, with mesh detail as a visual setting, so the Pi's Kaeli has fewer triangles than the PC's. A PC may show extra background people as a visual setting, as long as they never affect play.
3. **What zero means.** A test can only bound a rate: 1 clean minute shows at most one late frame per 20 s at 95% confidence, and it speaks only for that minute: the horde's first 5.7 minutes had 5 late and its full 10 had 1,517, so a number says which minute it covers. Ruled, jacob 11-02: *"3 hour soak? thats ridicoulous"*, then *"should make it 1 minute."* Budgets and margin carry the rest.
4. **Mode switches.** Ruled, jacob 11-03, on whether one counts as a dropped frame: *"no."* Behind a cut or fade only, and not counted as late.
5. **50 Hz TVs** can't physically show 60 images a second. jacob, 11-03: *"we have no 50hz pal tv to test on, mark this a to do for much later."* The to-do: restate the rule as one new image per field, offer PAL-60 first, and find a PAL volunteer.
6. **The rule during development.** jacob, 11-03: *"yes it will be awkward developing solely on the pi. the plan has it right. we can mark this as a todo for later."* It applies in play mode, with the agent and compiler stopped. A Pi can't hold it while compiling: memory misses by up to 0.27 GB, and a compile has already cost a flip.
7. **"Any Pi 4".** Ruled for now, jacob 11-02: *"nah i dont want to do that right now. my friend uses the exact same model we are using. this is good."* We prove on this model at stock speed, and older 1.5 GHz boards wait until shipping comes up. Still the default: state the cooling on the box, and exclude keyboard models from composite because they have no composite output.
8. **Online between different refresh rates.** A 480i Pi and a 60 Hz peer differ by one tick every 17 s, so something has to repeat or be synthesized. Default: decide in Phase 8.
9. **`lighting_rung.md`'s order.** Ruled, jacob 11-03: *"yes add the lighting, please, we need more capability and functionality here."* Phase 7 replaces its engine half, with no waiting on the Pi. Its game-feel work is game work and can continue in reva at any time.
10. **Does the dance floor stay in the engine?** Ruled, jacob 11-03: *"the dance floor is the free model asset showcase and it stays in the aigameengine."* The crowd's dancing, the beat clock and the venue are woven through the crowd loop. On moving them behind `engine.h`: *"i see your argument for taking it out, perhaps you are right."* So it stays for now, and that move is left open.

## Where the 22 gaps went

| Gap | Phase |
|---|---|
| Engine and games are one program; can't start empty; no starting point | 3 (the text split in 1) |
| No general game object | 2, then 3 |
| Game logic has nowhere to live | 3 |
| One player only | 6 |
| Screens hard-coded; UI and settings | 4 for setup, 6 for the toolkit |
| No way to ship | 8 |
| Colour, materials, lighting | 7, itemized there |
| Effects | 6 for games, 7 for the PC's richer versions |
| Collision and physics; animation; audio; camera | 6 |
| Time control | 1 for ticks, 6 for games |
| Large scenes | 2, then 6 and 7 |
| Loading and threads | 2 and 3 for loading, 6 for streaming, threads on a trigger |
| Networking | 8 |
| Editor | On a trigger |
| Developer tools | 0, 1 and 5 |

## New on the bench today

`reva_native/anim_bench/pi/` holds the instruments, uncommitted: `v3d_lab2.c` and `vk_lab.c` (the same real frame through GL and Vulkan, with live late-frame counts), `pi_phase0b.sh` (one section per measurement), `hash_run.py` (the PC's half of a hash compare), `v3d_lab.c` (what a frame can afford), `tube_lab.c` (mode timing, flip deadline, switch time, field parity), `gl_probe.c`, `gl_probe_pi.c` and `vk_probe_pi.c` (what each machine's graphics path offers), `vk_tube_test.c` (Vulkan's present rate on the tube), `gl_shim.c` (an LD_PRELOAD counter of draws, indices and late frames, no rebuild needed), `pi_run.sh` (a run that stops the console, watches memory and always brings the console back), and `cam_ctl.c` with `cam_luma.py` and `cam_sheet.py` for watching the tube through the PC's USB camera. The Pi has a copy of the engine at commit 87f5b6c in `~/engine_test`, 1.4 GB, apart from the console and the old `~/reva_native`. Rules for testing on real TVs are in the [all-TVs notes](../research_notes/Firestrider%20low%20level%20engine%20research/designing_for_all_tvs.md).
