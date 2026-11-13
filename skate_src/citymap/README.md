# The city cook

`skate_src/citymap/` turns OpenStreetMap data and USGS one-metre elevation into the engine's streaming world: a `cell` line
and a pack for each square of the map (`engine/stream.inc`), a far stand-in for the horizon, and marks. It knows no city.
The box, the origin, the files and the style come from a small JSON file the game owns; Bike Lane Bash's is
`map/seattle_start.json` in its own repository. Nothing here downloads anything, and it needs only numpy and Pillow.

It's the plan in `design/bike_lane_bash.md`, "THE MAP: HOW SEATTLE GETS BUILT", steps 1, 2, 3, 4, 5, 6 (in a first form) and 7.

## Running it

```
python -I skate_src/citymap/citymap.py GAME/map/seattle_start.json --raw D:/seattle_map/raw --out GAME
```

`--raw` is the folder the config's file names are relative to (or `CITYMAP_RAW`). The data are downloads, so run it with
`python -I` from any folder but theirs; `citymap.py` puts the user's numpy and Pillow back itself. `--out` is the game's
folder, and it writes there:

| | |
|---|---|
| `props/<prefix>_II_JJ.rpack` | a cell: column II east, row JJ north, from the box's south-west corner |
| `props/<prefix>_far.rpack` | the far stand-ins, a mesh a cell named after it, in the world's coordinates |
| `sets/<prefix>.set` | `climb`, `streaming`, `far`, `fardetail`, the marks and every `cell` line |
| `<prefix>_cells.txt` | each cell's triangles and bytes |
| `<prefix>_report.txt` | what it did, the checks' numbers, and how long each step took |
| `<prefix>_layers.txt` | step 6, the game layers: the road graph, the pickups (every name made up), the doors (`layers.py` says the format) |

The same input writes the same bytes: two cooks of the start area gave the same md5 over every pack and the set.
The start area (1024 x 1024 m, 256 cells) takes about 15 to 20 s on a desktop PC. The steps' times are the report's last line.

## What it does, step by step

1. **True size.** Every node's latitude and longitude goes to UTM metres (`geo.utm`, Krueger's series; WGS84 and NAD83
   are taken as one, a metre apart here). The origin is a node or a latitude and longitude. The engine's x is east,
   z is south, y is up, at `units_per_metre` (1.75). North is UTM grid north, which is about half a degree off true
   north at Seattle. The box is snapped out to whole cells, and the lowest ground in it is y 0.
2. **Ground and streets.** The elevation tiles are read as one field (`geo.Dem`, the GeoTIFF's own tie point, bilinear at
   pixel centres, holes filled from their neighbours) over the box and a margin. Each road and path on the ground gets a
   height along its centreline, the elevation's mean over `grade_smooth_m` either way (that's its grade), and is pressed
   into the field: flat across, back to the elevation over `road_blend_m` past its edge. Where two meet, a pixel goes to
   the one it's further inside. Then everything laid on the ground reads that one field, so crossing streets agree.
3. **Bridges from the tags.** Every way tagged `bridge` gets a height at each node: where a bridge way's end meets a way on
   the ground, the ground there; along it, a straight run by length; where bridge ways branch, one solve for the lot (the
   graph's Laplacian by conjugate gradients). A bridge's ground isn't pressed, so the ravine stays under it. Its deck is
   the road's surface at that height, sides and an underside `deck_m` below. OSM draws a bridge's road and sidewalks as
   separate ways, often with a gap between them, so where another deck is within `deck_reach_m` of an edge at its height
   the deck is filled across to it; where nothing is, there's a low wall (`parapet_m`).
4. **Lanes and rails.** `cycleway`, `cycleway:both`, `cycleway:left` and `cycleway:right` say `lane`, `buffered_lane` or
   `opposite_lane` (a green strip inside the road's edge, mesh `lane`), or `track` (a darker strip, mesh `track`, with a
   post every `post_every_m`, which are solid). `highway=cycleway` is its own path in the lane colour. `railway=tram` is
   two rails at the standard gauge (mesh `rail`). All flat, just above the road.
5. **Buildings.** Closed `building` ways and the outer rings of `building` multipolygons, and `building:part` ways (an
   outline with parts standing inside it is left to its parts). Height from `height`, else `building:levels` x `level_m`,
   else `building_default_m`; a part's `min_height` is its base. Walls stand from just under the footprint's lowest
   ground; the roof is the footprint cut into triangles (ear clipping), at the footprint's mean ground plus the height.
   Six flat colours by way id, one roof colour. ⛔ No names: nothing of a way's `name`, `brand` or shop tags reaches a
   mesh, a mark or a file, except a street's name and a house number in the game layers (step 6): real streets, fictional storefronts.
2b. **What the ground is where no way is** (`cover()`). Closed ways and multipolygons that say what an area is are painted onto the field, the
   biggest first so what's inside one wins: grass for `landuse=grass|flowerbed|recreation_ground|meadow|allotments|cemetery|forest`,
   `leisure=park|garden|pitch`, `natural=wood|scrub|grassland|shrubbery`; paved for `amenity=parking|parking_space|fuel|bicycle_parking`, any
   `area:highway`, a pedestrian area, `place=square`, `landuse=retail|commercial|industrial|construction|railway|highway`; a `surface` tag beats
   both. Where nothing says, the ground is paved within `pave_gap_m` (4 m) of a road's edge or a sidewalk in a built-up block (within `built_up_m`,
   40 m, of a building) and within `building_apron_m` (2 m) of any building; the rest is grass. Each 2 m square of the ground goes to the `ground`
   (grass) or `paved` mesh by what's at its middle, and the far stand-in the same.
6. **The game layers** (`layers.py`). The road graph: every road but a motorway, and every cycleway, node by node, with each edge's length, grade,
   class, one-way for a bicycle, whether it's a bridge, its bike lane (none, lane, track, path) and its street's name. A bridge's nodes are its
   deck's and joined to the street's where they're one OSM node. The pickups: whatever OSM calls a restaurant, fast food or a cafe in the box, at
   its door (a point inside a building goes to the nearest place on its wall and 0.7 m out), with a food from its cuisine and a name MADE UP from
   word lists, seeded by its id and checked against every name, brand and operator in the downloaded file. The doors: entrances and address
   points (and buildings that carry an address), with the house number and street. Each pickup and door names its nearest graph node. ⛔ Nothing
   of a business's own name, brand or shop tags is read but its cuisine; street names and house numbers are the map's.
7. **Cells.** The ground is made cell by cell (a vertex every `ground_step_m`); everything else goes, whole, to the cell its
   middle is in. Each cell's meshes are named by what they are with their colour, `road#3d3e42`, so they're told apart and
   need no texture: ground (grass), paved, road, bridge (a bridge's road surface), concrete (a concrete street's), path, sidewalk (raised `curb_m`,
   with its curb faces), crossing, curb, lane, track, post, rail, deck, parapet, roof, and bldg0 to bldg5. Each cell's
   triangles and bytes are printed against `budget`. The far stand-in is each cell's ground every `far_step_m`, its main
   streets, and its tallest `far_buildings` buildings as boxes.

The set says which meshes are which kind of surface (`surface <kind> <words>` lines, from `surfaces` in the style): road, lane, track, sidewalk,
crossing, rail, grass, paved, path, deck, curb, wall, building. The engine reads them into the walked world, and `E->surface` says the kind and
the slope at a point (`engine/spans.inc`).

The marks: `marks` in the config, each a latitude and longitude, `on` the `ground` or the `top` (the highest of the ground
and any deck there), `above` it in metres. `views` are pairs of marks, `view_<name>` and `view_<name>_at`, for a lens, and
an optional `view_<name>_hero`. `checks` says which bridges to measure (the deck over the ground at mid span) and a
profile (the road graph's shortest way between two places, its height every `step_m`).

## The style, and what it assumes

Every value is in `STYLE` at the top of `cook.py`, and the config's `style` says any of them again. What it assumed for
Seattle:

- Widths: a way's `width` tag; else its `lanes` x 3.3 m; else by class, curb to curb: motorway 11 m, primary 13,
  secondary 11, tertiary 10, residential and unclassified 8, living street 6, service 4.5, busway 7, links 5.5 to 6;
  sidewalk 2 m, crossing 3, footway 1.8, path 1.5, steps 2, cycleway 2.5, pedestrian street 5.
- Sidewalks: OSM's `sidewalk*` tags; `separate` means the sidewalk is its own way (most of Seattle is mapped that way).
  A road that says nothing gets both, 2 m wide, if it's primary, secondary, tertiary, unclassified, residential or a living
  street. A sidewalk is 0.15 m above the road.
- Buildings with no height or levels are 8 m; a level is 3.2 m. In the start area that's 266 of 357.
- Cells are 64 m; they come in at 280 units (160 m) and go at 340; the far stand-ins show out to 3000 units, and the far
  plane is 3400.
- Grades: a 12 m mean either way. Road edges blend back to the elevation over 4 m, paths over 1.5 m.
- A deck is 1.4 m thick; its wall is 0.3 m by 1.1 m. Posts are 0.12 m and 0.9 m tall, every 6 m.
- (11-06) Piers (`piers()`): where a deck passes over a road, a rail or a path on the ground (a sidewalk mapped as its own way too), a bent stands
  `pier_side_m` (2 m) past that way's band on either side, or one in the middle of a gap too narrow for two; a way running along the deck (its own
  continuation on the ground) isn't crossed. Between, a bent every `pier_every_m` (30 m), half a spacing in from each way's start. A bent is round
  columns `pier_m[0]` (1.2 m) across, about one for each `pier_across_m` (7 m) of the deck's width, under a cap beam the deck's width. No column on a
  way, in a building, through a lower deck, or where the underside is less than `pier_clear_m` (2.6 m) over the ground. `pier_every_m` 0 is none.
  The rule is all of it: nothing is from a photograph. `CITYMAP_PIERSAY=<mark>:<metres>` says, for each bent that near a mark, where it is and what
  stopped each column.
- (11-06) THE LOOK. All by rule from the map's tags and sizes and palettes of mine, all made by code;
  nothing is from a photograph, and a kind that carries its own colour is named with it (`wall#9b5a44`), so it needs no texture. Each has a switch.
  - `facades` (1): a building's family from its tags and size (house, apartments, brick, commercial, concrete, industrial, a glass tower) picks its
    walls' colour (its `building:colour` wins) and its windows: rows on every floor (a family's width, height, sill and spacing), a glass tower's
    floor bands instead. A building with a shop or a restaurant in it (a node of the map's) has shopfronts (`shopfronts`, 1; 0 is the old glass on
    every wall, the control): on each wall a street runs along within `shopfront_reach_m` in front of it (not a service road), or else on the one wall
    that faces one best; the wall in bays of 10 m or less, the map's shops in the building each on the front nearest it, a bay its window, a sign
    over it and on some an awning. The sign says what the map says the shop is (`sign_words()`: its `shop`, `amenity` and `cuisine` in common words,
    a closed one FOR LEASE), never its name, in the colours a street's signs come in, its letters Be Vietnam Pro (below). A cell's signs share one
    texture (`sign_atlas()`, a row of 512 x 128 each), so they're one mesh. A shop building that gives its floors gets a taller
    ground floor (`shop_floor_m`, 4.6), its upper windows over it. A flat roof
    over 5 m has a parapet and, when it's big, rooftop units; a house, or a `roof:shape` that says so, a gable over the box round its footprint
    when the footprint is near enough that box. The far stand-ins take each building's wall colour.
  - `trees` (1): the map's trees and tree rows; woods (`plant_wood`), scrub (`plant_scrub`), parks (`plant_park`) and lawns (`plant_lawn`) planted on a
    jittered grid ([spacing, share]); grass steeper than `steep_grass` (35%) wooded as well (`plant_steep`), because the map leaves embankments and
    ravines as grass. Low-poly trees, conifers and shrubs, kept off every way, building and too-low deck.
  - `street` (1): lamps (the map's, and along arterials every `lamp_every_m` where it has none), a signal pole with its arm and head for each road
    coming into one of the map's signals, stop signs, bus-stop poles, benches, bins, bollards, fences, walls, retaining walls, guard rails and
    jersey barriers from the map's barriers; crosswalk stripes on a marked crossing (its band is the road's colour now), a double centre line on a
    two-way arterial, lane lines from `lanes`, none within 9 m of a junction.
  - (11-06, after KartaView's photos) wooden utility poles along a street (`utility_every_m`, one side by the way's id) with a
    crossarm, a transformer on some, and four wires sagging pole to pole; trolley-bus wires (a pair over the right-hand lane each way) hung from
    span wires between poles at the curbs, wherever a road says `trolley_wire=yes`; the streetcar's wire over each tram track; a green
    street-name sign (no words) at a corner where two named streets meet; cars parked along the curbs (`car_every_m`, `car_share`) where the
    map's `parking:*` tags allow it, or on side streets when they say nothing, not across a crossing, a driveway, a junction, a bus stop, a signal
    or a bike lane. Signal heads are black. A crossing's band is the colour of the road it crosses. Woods are brushy and scrub is bushes with a
    few small trees. The config can recolour (`style.colours`): Seattle's roads are a sun-faded grey.
  - (11-06, 12th and Jackson) a crosswalk is marked the way the map's `crossing:markings` says:
    `lines` its two edges, `dashes`, `ladder` edges and bars, `zebra` bars, `:paired` or `:double` bars in pairs, nothing said bars. A bike box
    (`cycleway=asl`, drawn as an area) is green paint. A bus stop with `shelter=yes` gets a glass shelter behind its pole: a back and two ends, a roof on
    four posts, its bench inside and its bin beside if it says so. A tram stop's platform drawn as an area is a slab at kerb height with a canopy over
    a windscreen (`shelter=yes`), a bench (`bench=yes`) and a ticket machine. A trolley span wire hangs from a pole that's already standing within
    `joint_pole_m` (a signal's, a lamp's, a utility pole) before a new one is put up, and where two wired streets meet, the turns' wires are strung over
    the junction from each way in to each way out along a street of another name. Cars stand in the surface lots, square to each mapped parking aisle,
    in `lot_car_share` of the stalls. Fire hydrants are pillars in the map's colour (yellow if it says none); a flagpole flies the flag its
    `flag:wikidata` names in `style.flags` (bands top to bottom), or a plain one in its `flag:colour`.
  - (11-06) A building that says neither its height nor its floors takes them from its kind (`default_levels`: retail one floor of 4.6 m, a kiosk,
    a supermarket, garages, a shed, a carport), else `building_default_m`. The config's `tags` correct the map's tags before anything reads them, each
    with its `_source` (the BEAM at 1029 S Jackson St is seven storeys: the Daily Journal of Commerce).
  - (11-06) The config's `hotspots` (a node, a radius, how many people, how many tents) are laid out as `spot` lines in the game layers
    (`layers.py`): on the sidewalks within the radius, the same every cook, more by a bus stop, along a fence and by a shop (a corner store most),
    none on a crossing, a kerb's ramp or inside a bus shelter, fewer further out; in groups talking, alone standing, folded, crouched, kneeling over goods,
    sitting, lying, at a cart, and a tent. Each says where, which way it faces, what, and its group; the game puts people there (Bike Lane Bash's
    `blb_street.inc`).
  - (11-06) A fence is what the map says it's made of: chain link (`fence_type` chain_link, wire, electric, or nothing said and not wood) and bars
    (railing, bars, metal) are see-through, a two-sided panel with texture coordinates along it carrying a weave made by code (`weave_texture`: holes clear),
    written as its own textured mesh in the cell's pack and drawn blended by the texture's alpha (`weave_blend`), so it's a veil at a distance and not gone or
    a checker; posts, a top rail and a tension wire are solid. Wood is a solid panel. Every surface lot (`amenity=parking`, `parking=surface`) is its own
    ground, asphalt (the cover's class 3), with its stalls' lines along each mapped aisle. A colour with an alpha (`#rrggbbaa`) is drawn sheer, blended:
    the bus shelters' and the tram platforms' glass.
  - (11-06) What a source shows that the map doesn't, in the config with its `_source`: `tags` can set `crossing:mural` (a crosswalk painted as a
    mural: one picture laid across it, our own design, `mural_texture`), and `crossing:markings=surface` is a band in `crossing:markings:colour`;
    `features` places a chain-link `fence` along points, or a `bin_pen` (a chain-link pen of wheeled bins, its gate on its south side, trash
    heaped in front). 12th and Jackson's come from the city's traffic camera there (Bike Lane Bash's `map/seattle_start.json`).
    A `shopfront` names a building (`way`) and a point by one of its walls (`wall_at`), and lays that wall's front as the source shows it instead of
    the rule's: `bays` from its end toward `from` (north, south, east, west), each `m` long, a window or `boarded` (plywood painted with a mural of
    our own design, `boards_texture`: three in turn, a texture a 4.88 m run) or `plain` (bare wall), with its `sign` ([words, background,
    letters, height, width]) and its `awning` (a colour). A `billboard` stands at `at` on two posts, its face's bottom `bottom_m` over the ground,
    looking toward `faces_deg`, `size_m` [width, height], its `words` (two lines) in its `colours`; its back is a steel box. A `fence` with
    `razor` has coiled razor wire along its top (two crossed panels carrying coils made by code, `razor_texture`, blended like the weave).
    (11-06, the second accuracy run) `litter` scatters `count` pieces of paper, cups, cans, foil, wrappers and bags over an area (a `way`'s ring, or
    `area` points); `sign` with `what: school` stands a school crossing sign and its SCHOOL plate on a post at `at`, facing `faces_deg` (its faces drawn
    by code, `school_texture`); `bins` stands the city's street bins side by side (`which`: garbage black, recycle blue), their labels toward
    `faces_deg`; a `shopfront`'s `piers` colours the wall's lower part behind its windows; a bin pen has boards leaning in it and yellow bags with the
    black. The config's tags can give a surface lot its own `lot:car_share` and put its cars on one side of its aisle only (`lot:car_side`).
    A `crest` names a building (`way`) and lists `items`, each on the wall nearest its `wall_at`: a raised `gable` [width, height] over the roofline
    in the wall's colour, peaked, with a trim band at its shoulders and an `emblem` (a colour: a medallion of ours, `emblem_texture`), or
    `lettering` [words, colour, height, width] along the wall over its ground floor, bare letters (`letters_texture`, drawn blended). The gable is
    solid (`_wall_solid`): a raised block, a low triangle on it, a cornice along its shoulders and both rakes; an emblem `ji:<colour>` is the
    character 吉 drawn by code (`ji_texture`). A building tagged `facade:high_windows=yes` is one tall shop storey with a row of high windows over
    its signs; `facade:cornice` (a colour) gives every wall a deep cornice and a thin moulding under it, `facade:band=yes` a band under the upper
    windows, `facade:windows` its own windows (width,height,sill,spacing) and `facade:mullions` that many mullions in each, in a dark frame.
  - (11-06) The config's `districts`: a neighbourhood as the city's map draws it (an OpenStreetMap boundary relation's outer ways, `relation`),
    widened by `buffer_m` so both sides of a street on its edge are in it, and what's its own there. `lamps: lantern` (Chinatown-International
    District): every street's lamps are red lantern posts, `lantern_every_m` a side with the sides staggered, in place of the grey lamps (a lamp the
    map has there is one too): an octagonal foot, a slim red pole, a lantern of pale glass under a red pagoda cap and a gold finial, and on a gold arm
    toward the street a banner of ours (`banner_texture`). `signs: vi` (Little Saigon): a shop's sign says its kind in Vietnamese over the English
    (`VI_PAIRS`), and each street-name blade has a brown plate under it saying the street in Vietnamese (`street_vi()`: South Jackson Street is Nam
    Đường Jackson and 12th Avenue South Đường Số 12 Nam, as the corner's own plates read; other names the same way). Every street-name blade says
    its street, the city's short way (`street_short()`: 12th Ave S), on both faces. Every sign is lettered in Be Vietnam Pro Bold (`fonts/`, SIL Open
    Font License: `fonts/README.md`), which has every Vietnamese letter (`sign_font()`); a sign `A|B` is two lines, the first big.
  - (11-06) A hot spot's `band` (`people`, `step_m`, `second_row`): along every fence within its radius (the map's and a source's, a bin pen's sides),
    on the fence's street side (the side a street is on, never a service road: a lot's aisle can be nearer), a place every `step_m` on the sidewalk or
    the paved strip behind it, nearest the hot spot's node first; the first row against the fence in a mix read off photos of 12th and Jackson
    (sitting on the ground most, hunched over the hands, standing, crouched, folded, a wheelchair, goods, lying), a second row in places, neighbours a
    group turned to each other; the groups elsewhere keep 4 m off it. One or two by each stop's pole face the street (waiting for a bus), and a canopy
    someone sells under stands by each stop the hot spot's `canopy_stops` names (by its `ref`). The cook says how many places the band had and why it
    turned the others down.
  - The ground's grass in three tones and paving in two, in slow patches.
  - `light` in the config goes into the set: `fog` (density a unit [r g b]: the city ends in air), `sky`, `ground`, `haze`, `exposure`, and `sun`
    only for a game that doesn't light by its own hour.
  ⛔ A made piece's faces are turned out from its middle in the engine's coordinates (`_orient`): those flip north, so a winding that faces out in
  map terms faces in there.

## For a Godot game (11-11)

For the skate game `dont get dropped`, which runs in Godot: `CITYMAP_GODOT=<folder>` (and
`CITYMAP_GODOT_NAME`, default Seattle) makes the cook also write `SM_<name>.glb` and `SM_<name>.park.json` there (`godot.py`). In METRES, x east, y
up from the box's lowest ground, z south: glTF's and Godot's own handedness. The meshes are gathered by what they are, in blocks of 4 x 4 cells, and
named the way that game's park loader (`addons/skate_system/skatepark_loader.gd`) reads names: `SM_<name>_Terrain_*`, `_Roads_*` and `_Skate_Walks_*`
are ridden, `_Buildings_`, `_Cars_`, `_Plants_`, `_Wires_`, `_Street_` and `_Signs_` are obstacles. A kind's colour is a cell of one small palette
texture that its vertices point into, so the city is one material (and one more for the sheer glass); what carries a texture keeps it. ⛔ Not vertex
colours: Godot's glTF importer doesn't use COLOR_0 as the albedo, and the first export came out white in the game (11-11). The sidecar's `rails` are the tops of the low walls,
retaining walls, guard rails and jersey barriers, to grind; `marks_m` are the config's marks in the same metres. No business's name is in it, as
anywhere. The slice: 81 MB, 1.54 million triangles, 193 colours, 54 rails. Credit OpenStreetMap (ODbL) and the USGS.

## What stands in a roadway (11-10)

`audit()` runs at the end of every cook and changes nothing: it says. Everything made is held against every road's own band (`roadr()`: the roads as
pictures of the field, a metre a pixel, how far inside the nearest road's edge each place is, bridges against their own decks). A face between
`audit_low_m` (0.35) and `audit_high_m` (2.2) over the road's surface, more than 0.2 m inside its edge, is something standing in the roadway. The
report's line counts the places in a travel lane (more than `audit_lane_m`, 3.0, in from the edge) and in a curb lane, by kind; `<prefix>_roadways.txt`
lists each with its place, its depth and its road. ⛔ Read it after any change to what the cook stands along a street: Bike Lane Bash's slice had 145
places in travel lanes before anyone looked.

The rules it led to, all by `in_road()`, `road_in()`, `deck_over()` and `junctions()`:
- **A barrier never stands across a way the map draws through it.** A barrier that crosses a way on the ground and runs within `barrier_twin_m` (4) of
  one that says its `layer` for most of its length is up on that structure too, and left out (OpenStreetMap way 1478810952, an I-5 edge wall with no
  layer). Any other is cut where a road or a path crosses it, a gap the way's band wide. A jersey barrier `barrier_closure_m` (30) long or less is
  left whole: that one closes the street.
- **Nor along inside a road's lanes.** Up to `barrier_snap_m` (3.2) inside a band it's moved out to the band's edge; deeper it's left out; so is
  what would stand in a bridge's lanes where its deck is at the ground. The report says how many metres of each, by the road's class. Jersey barriers
  are left alone.
- **The median of a divided street** (`in_median()`): a spot with the nearest roadway on one hand and another way of the same name on the other,
  both within `dual_m` (25), gets no lamp, lantern post, utility pole, street-name sign or parked car, and a trolley span's pole there is a bracket
  arm from the outer curb.
- **Poles and signs are never in any road**, not only clear of the nearest one's line: a signal's pole backs along its own street until it is, a
  stop sign stands at the kerb on the right of the way toward the nearer junction (or as its `direction` says), a mapped lamp in a road is left out.
- **A protected lane's posts** stop `post_junction_m` (11) short of where roads meet, and none stands in another road's lanes.
- **Decks:** no utility pole under a deck lower than the pole, no wire span with a deck in its line, no signal pole up through a ramp, nothing planted
  in a bridge's own roadway where it comes down to the ground. A lot's car never has any of itself out in a street.
- `bare_under_m` (off): bare ground deep under a wide deck (`under_decks()`).

## What it doesn't do yet

- Turn lanes, stop bars, arrows, parking stalls drawn. A lot with no mapped aisles has nothing parked on it, and a lot is the paved colour, not
  asphalt. No moving traffic. Chinatown's signs are English only: Chinese letters wait for a font that has them (Be Vietnam Pro has none). The
  ground's cover is decided on 2 m squares, so its edges are stepped.
- Buildings: no doors drawn, no holes in footprints, hipped and other roofs as gables, windows on walls that face another building too, and the
  far stand-ins are boxes in a building's colour with no windows.
- Trees by rule where the map has none: not the real canopy (USGS lidar would say where it is, not downloaded).
- A bridge's piers are the rule above, not the real structure's: no arch (the Jose Rizal's is in OSM as `man_made=bridge`, unread), no abutments.
  A bridge's height is a straight run between where it meets the ground, so a long viaduct with a hump or a dip isn't
  that shape, and two bridges crossing in plan aren't checked against each other.
- Tunnels are left out entirely. Railways other than `tram` aren't drawn.
- Where a road's mapped width is less than the street really is (lanes=2 on part of the Jose Rizal, Jackson's 4 lanes with
  no parking), the gap to a separate sidewalk is pressed ground (paved in a built-up block), and the deck fill covers it only on bridges.
- No curb cuts: a raised sidewalk crosses a driveway at 0.15 m, which a body steps up but a wheel will feel.
- Step 6 has no crossings or sidewalks for the crowd's routes yet, no turn restrictions or signals in the road graph, and a door is a point,
  not a building's real entrance unless OSM maps one. A made-up name is checked only against this file's names, not the world's.
- Where two streets' grades meet in an intersection the pressed ground can crease (a pixel goes to one street or the other), so the slope
  `E->surface` gives at one point there can read 10 to 16% on a flat corner; a wheelbase's two heights are steadier.
- The whole downloaded box is the same config with a bigger `area`; only the start area has been cooked and looked at.
  Expect the cook's time to grow with the area (the pressing and the laying are per way).
