# Building the Seattle map

This is how the Seattle map in **dont get dropped** is made, and how to make it better or bigger yourself. The first
part is for you, the people on the team. The second part is for the AI you work with (Claude, Grok, GPT, Cursor):
paste it in or point it at this file.

It lives in the aigameengine repo as `skate_src/citymap/MAP_GUIDE.md`, beside the program it describes. The game's
repo has a copy as `docs/SEATTLE_MAP.md`; when the two differ, the aigameengine one is newer.

The map isn't modelled by hand. A Python program, **the city cook** (`skate_src/citymap/` in the aigameengine repo),
reads two free public datasets and builds the whole city from them by rules:

- **OpenStreetMap**: every street, sidewalk, crosswalk, building, bike lane, fence, bus stop, tree and traffic light
  people have mapped, with tags saying what each is.
- **USGS elevation**: the real ground's height every metre, so Beacon Hill, the ravine under the Jose Rizal Bridge and
  the climb to Pacific Tower are the real hills.

Out of it comes one `.glb` model and one small `.park.json` file. Those drop straight into the game's park loader.
Change the rules or the config and cook again; the same input always gives the same files, byte for byte.

---

## Part 1: for the team

### What you need

| What | Where | Notes |
|---|---|---|
| The aigameengine repo | `github.com/nemonicicon/aigameengine` (private: ask for access) | The cook is `skate_src/citymap/` |
| Python 3.11 or newer | python.org | 3.14 is what it's built on |
| numpy and Pillow | `pip install numpy pillow` | The only two libraries it uses |
| The map data | downloaded once, below | About 610 MB on disk |
| About 2 GB of free memory | | A cook of the 1 km slice peaks at 1.4 GB; see "How big" below |
| Godot 4.7 and the game | `nemonicicon/DontGetDropped` | To play what you made |

### Downloading the map data

Make a folder for it **outside both repos**, for example `D:\seattle_map\raw\`, with two folders in it: `osm\` and
`dem\`. Never put the downloads inside a repo: OpenStreetMap's licence travels with the data, and the elevation tiles
are hundreds of megabytes.

**1. OpenStreetMap (about 57 MB).** The box is south 47.565, west -122.335, north 47.635, east -122.295: the
International District, Little Saigon, the north end of Beacon Hill and Capitol Hill. It comes from the Overpass API.
Save this query as `osm_query.overpassql`:

```
[out:json][timeout:300][maxsize:1073741824];
way(47.565,-122.335,47.635,-122.295)->.w;
node(47.565,-122.335,47.635,-122.295)->.n;
(.w; .n; node(w.w););
out body qt;
relation(47.565,-122.335,47.635,-122.295);
out body qt;
```

Then download it from the folder you made (on Windows use `curl.exe`, not PowerShell's `curl`):

```
curl -A "dont-get-dropped city map" -o osm/box_47.565_-122.335_47.635_-122.295.json --data-urlencode data@osm_query.overpassql https://overpass-api.de/api/interpreter
```

The `-A` part names the program asking. Overpass refuses a request that doesn't (`406 Not Acceptable`), so keep it,
and change the words to your own project's name if you like. It takes a minute or two. The file must be named exactly that, because the config names it. If the server says it's
busy, wait a few minutes and try again; don't hammer it. Write down the date you downloaded it: OpenStreetMap
changes every day, and if anyone who gets the game asks which map data it used, we have to be able to give them
this file (the ODbL licence says so). Keep it.

**2. USGS one-metre elevation (two files, about 550 MB).** Straight from the USGS's own storage, no account needed:

```
https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/1m/Projects/WA_KingCounty_2021_B21/TIFF/USGS_1M_10_x55y528_WA_KingCounty_2021_B21.tif
https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/1m/Projects/WA_KingCounty_2021_B21/TIFF/USGS_1M_10_x55y527_WA_KingCounty_2021_B21.tif
```

Put both in `dem\`. They should be 238,454,157 and 314,111,440 bytes. Each is a 10 km square of ground heights in
metres. Buildings and bridges aren't in it: it's bare earth, and the cook puts the bridges back.

These two cover the whole download box except a strip about 10 m wide along its west edge. Going further west
(Pioneer Square, the stadiums, the waterfront) needs the two `x54` tiles beside them in the same folder:
`USGS_1M_10_x54y528_WA_KingCounty_2021_B21.tif` (152,270,817 bytes) and `..._x54y527_...` (193,386,252 bytes), and
their names added to the config's `dem` list. To find tiles for anywhere else, use The
National Map's downloader (`apps.nationalmap.gov/downloader`), choose Elevation Products (3DEP), "1 meter DEM",
and draw your box.

Your folder should end up like this:

```
D:\seattle_map\raw\
    osm\box_47.565_-122.335_47.635_-122.295.json
    dem\USGS_1M_10_x55y528_WA_KingCounty_2021_B21.tif
    dem\USGS_1M_10_x55y527_WA_KingCounty_2021_B21.tif
    osm_query.overpassql
```

### Cooking the map

From the aigameengine folder:

```
python -I skate_src/citymap/citymap.py skate_src/citymap/examples/seattle_slice.json --raw D:/seattle_map/raw --out D:/seattle_map/cooked --godot D:/seattle_map/godot --name Seattle
```

- `--raw` is the download folder; `--out` is a scratch folder for the engine's own files (you can ignore them);
  `--godot` is where the two files for the game go.
- `-I` keeps Python from running anything that happens to sit in the folders it reads. Use it with downloaded data.
- It takes about two minutes and prints each step. At the end it says
  `for Godot: .../SM_Seattle.glb, 81 MB, ...` and `the cook took ... s`.
- The full report is in `--out` as `sea_report.txt`.

### Putting it in the game

The park is in the game on the branch `feat/seattle-slice` on GitHub; until it's merged into `main`, check that
branch out to play it. It adds four things, which is also all it takes to add a park from scratch:

1. `assets/models/SM_Seattle.glb` and `SM_Seattle.park.json`: the two files the cook made (Godot also unpacks the
   model's textures beside it, `SM_Seattle_T_Seattle_*.png`).
2. `maps/seattle_slice.tscn`: a sun, an overcast sky, a safety net under the city, a `Skatepark` node running
   `addons/skate_system/skatepark_loader.gd` with `town_path` and `sidecar_path` set to those two files and
   `visual_chunk_size` 64, and the `Player` standing on 12th Ave S at Jackson (x -0.4, y 48.6, z -0.5) with
   `play_area` the box (`Rect2(-512, -256, 1024, 1024)`) and `void_y` -6.
3. A row in `maps/premade_maps.gd`, with `"web": false` so the browser build leaves it out (it's big), and its files
   in the Web preset's `exclude_filter` in `export_presets.cfg`.
4. `tests/verify_seattle_slice.gd`, and a line in `CREDITS.md`.

To update it, copy your two new files over the old two, open the project in Godot (it re-imports the model by
itself), and check:

```
godot --headless --path . -s res://tests/verify_seattle_slice.gd
```

That puts a skater at 12th and Jackson, on Dearborn under I-5 and on the Jose Rizal Bridge, rolls each one and says
whether it stayed on the ground. Then play it: PARKS, SEATTLE: 12TH & JACKSON. It's left out of the browser build
on purpose (it's big), with `"web": false` on its row.

What the game does with the model's pieces is in their names: anything named `..._Terrain_...`, `..._Roads_...` or
`..._Skate_Walks_...` is ridden on; buildings, cars, plants, wires, street furniture and signs are things you hit.
The sidecar's grind lines are the tops of the low walls, retaining walls, guard rails and jersey barriers.

### Making it better

Almost everything is changed in **the config**, `skate_src/citymap/examples/seattle_slice.json`, without touching code. Copy it, change
your copy, cook it. What's in it:

| Key | What it does |
|---|---|
| `area` | The box to cook, in metres from the origin (`east_m`, `north_m`). The slice is 1,024 m square. Bigger is slower and heavier: see "How big" |
| `origin` | The map's (0, 0): an OpenStreetMap node, here the traffic light at 12th Ave S and S Jackson St |
| `marks` | Named places (a latitude and longitude). They come out in the sidecar's `marks_m`, for spawn points |
| `style` | Overrides any rule's number (every one is listed at the top of `cook.py`, in `STYLE`), and the colours |
| `light` | Fog and sky, for the engine. The Godot scene sets its own |
| `tags` | Corrections to OpenStreetMap: `"way/906291534": {"building:levels": "7", "_source": "where you learned it"}` |
| `features` | Things a photo shows that the map doesn't: a `fence`, a `bin_pen`, a `shopfront`, a `billboard`, `litter`, a school `sign`, the city's `bins` |
| `crest` | A building's gables, emblems and lettering (Ding How Shopping Center's) |
| `districts` | A neighbourhood's own look: Chinatown-ID's red lantern posts, Little Saigon's Vietnamese signs |
| `hotspots` | People's places at 12th and Jackson. That's for Bike Lane Bash; the skate game can ignore it |

`skate_src/citymap/README.md` says what every key and every rule does, in detail.

**The best fix is often in OpenStreetMap itself.** If a building's height is wrong or a curb is missing, you can
correct it on openstreetmap.org (free account), and everyone's maps get better. Download again afterwards to pick it
up. Or, for something only our game needs, put it in the config's `tags` with a `_source`.

### The rules about sources

These are firm. They come from the services' own terms and from the project's own rules.

- **Never Google Street View or Google Maps.** Google's terms forbid screenshots of Street View and building anything
  from it. Not for reference, not traced, not kept.
- **Fine to use:** OpenStreetMap; the USGS; your own photos; **KartaView**'s street photos (CC BY-SA 4.0, credit
  "© Grab and KartaView Contributors"; look, don't store them in a repo); **Mapillary** with an account (look at its
  terms first); the King County Assessor's records for a building's storeys and age (facts only, never an owner's
  name).
- **Seattle's traffic cameras** (SDOT, also on sfdlive.com): only while you're working on the stretch a camera watches,
  one look per camera every 10 minutes, keep nothing, trace nothing, identify nobody.
- **YouTube:** watch it yourself and say what you saw; no downloading or grabbing frames.
- **Real streets, made-up businesses.** No real shop or restaurant name goes anywhere a player can see. Signs say what
  kind of shop it is (GROCERY, PHO, BODY MASSAGE), never its name. The cook already does this; keep it that way in
  anything you add.

### Credit

The game must credit **© OpenStreetMap contributors** (Open Database License) and the **USGS 3D Elevation Program**.
`CREDITS.md` in the game already has the line. The sign lettering is Be Vietnam Pro (SIL Open Font License,
`skate_src/citymap/fonts/OFL.txt`).

### How big

The 1 km slice: about 2 minutes, 1.4 GB of memory at its peak, an 81 MB model of 1.5 million triangles, a 6 second
park build in Godot. Time, memory and size grow with the area, roughly with the number of streets and buildings in
it. The whole download box is about 3 km by 8 km; don't cook that in one go. Grow the box a step at a time and watch
the report's numbers. On the skate game's side, every mesh gets collision when the park loads, so more city is more
load time.

### What it doesn't do yet

- No moving traffic or people. The cars are parked; the street's people are Bike Lane Bash's and aren't in the model.
- The edge of the box is a drop: past it there's nothing.
- Roofs are flat or simple gables; buildings have windows by rule, not their real ones; trees go where the map has
  them, or by rule.
- Tunnels are left out; railways other than the streetcar aren't drawn.
- `skate_src/citymap/README.md`'s "What it doesn't do yet" has the full list.

---

## Part 2: for your AI

You're helping build the Seattle map for a Godot 4.7 skateboarding game, **dont get dropped**
(`nemonicicon/DontGetDropped`). The map is generated by **the city cook**, the Python in `skate_src/citymap/` of the
`aigameengine` repo. Read `skate_src/citymap/README.md` before changing anything: it describes every rule and every
config key.

**The pipeline.** `citymap.py CONFIG --raw RAW --out OUT --godot GODOT --name NAME` runs `cook.Cook(cfg, raw, out).run()`:
read the OpenStreetMap JSON and the GeoTIFF elevation (`geo.py`); sort the ways (`classify`); give roads their grades
and press them into the ground (`profiles`, `carve`); decide what the ground is (`cover`); lift bridges (`bridges`);
make the ground, roads, sidewalks, curbs, lanes, decks (`ground`, `lines_geometry`), piers, buildings (`facade`,
`shopfronts`), plants (`planting`) and street furniture (`street`); check every road for things standing in it
(`audit`); write the engine's cells (`write`), the game layers (`layers.py`), and, with `--godot`, the glTF and the
sidecar (`godot.py`). Every number is in `STYLE` at the top of `cook.py`; the config's `style` overrides any of them.

**Coordinates.** In the Godot files: metres; +x east, +y up, +z south (glTF's and Godot's own handedness); y 0 is the
lowest ground in the box; (0, 0) is the config's origin. Inside the cook, map terms are (east, north) in metres from
the origin, and heights in metres above sea level; `cells.xyz(e, n, h)` turns them into the engine's units (1.75 a
metre, z = -north). Don't mix the two.

**How the game reads the model** (`addons/skate_system/skatepark_loader.gd` in the game): every `MeshInstance3D` gets
trimesh collision; a node whose name contains `Terrain`, `_Roads` or `_Skate` is on the ride layer, anything else is
an obstacle. `godot.py`'s `RIDE` and `OBSTACLE` tables decide which name each kind of mesh gets: a new ridable surface
must land in `RIDE`. The sidecar (`format: skatepark_sidecar`) carries `rails` (polylines in park space, welded into
grind lines by `GrindRailGraph`), `gaps` and `walls` (both empty for now) and our `marks_m`.

**Rules for changing the cook.**
1. **Data, not code, first.** A fix that belongs to one place goes in the config (`tags` with a `_source`, or
   `features`). A fix that's true everywhere is a rule in `cook.py`, with a `STYLE` key for its number.
2. **Deterministic.** The same input must give the same bytes. No unseeded randomness; seed from an OSM id or a
   position. Iterate dicts in sorted order. Check: cook twice and compare the files' hashes.
3. **Nothing from a business's name.** The cook never reads `name`, `brand` or `operator` except a street's name and a
   house number. Signs show what kind of shop it is. Keep it so.
4. **Sources.** Never Google Street View or Google Maps, in any form. OpenStreetMap, the USGS, the team's photos,
   KartaView (CC BY-SA) and public records' facts are fine. Read a source's licence before using it.
5. **Run downloaded data with `python -I`**, from a folder that isn't theirs. Never write into the raw data folder.
6. **Every new rule says itself.** A line in the report (`self.say(...)`) with how many it made and why it skipped
   any, and a paragraph in `README.md`.
7. **Read `<prefix>_roadways.txt` after any change to what stands along a street.** It lists everything standing in a
   road's lanes. The slice has 0 in travel lanes; keep it there.

**Checking a change, in this order.**
1. Cook. Read the report's last lines: `the roadways:` (travel lanes should say nothing), `cells:` (none over budget),
   `for Godot:` (size and triangles), `the cook took`.
2. Cook again and compare hashes of the outputs: they must match.
3. Copy the two Godot files into the game, then `godot --headless --path . --import` and
   `godot --headless --path . -s res://tests/verify_seattle_slice.gd`. Also `verify_parks_menu.gd` and
   `verify_web_packs.gd`, which check the menu and that the park stays out of the browser build.
4. Look at it. A test that passes says the skater didn't fall through; only eyes say whether it looks right. Ask a
   person to play it.

**Known traps.**
- OpenStreetMap draws a divided street as two one-way ways, and a bridge's road and sidewalks as separate ways.
  Rules that put things "at the curb" must handle both (`in_median()`, the deck fill).
- A barrier mapped without its `layer` tag looks like it's on the ground. The cook drops one that runs beside a
  layered twin and crosses a road.
- The elevation is bare earth: bridges and overpasses aren't in it.
- `cover()` decides the ground on 2 m squares, so its edges are stepped.
- Pillow reads a whole elevation tile into about 400 MB. Two tiles plus the cook's own fields are most of the memory.
- Colours are a palette texture, not vertex colours: Godot's glTF importer doesn't use a mesh's `COLOR_0` as its
  colour, and the first export came out white in the game. A new kind of thing takes its colour from the `#rrggbb` in
  its kind's name, and `godot.py` gives that colour a cell in the palette by itself.
- In the game, collision is built for every mesh: thousands of small meshes cost load time. `godot.py` gathers
  meshes by kind into 4 x 4 cell blocks (256 m) to keep the count near 150.
