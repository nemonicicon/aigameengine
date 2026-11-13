# What a character has to have

2026-11-04, while queue step 23 was being built: a bought character is the reference for a character creator, starting with REVA. She has most of the features a model should have, and is held as the standard, or improved to ours. And REVA is rebuilt from her reference sheets with a better look, because her model then was not good enough.

So this is the standard: a list of things a body has or hasn't, each read off its pack by one command, with the bought character's own answers as the bar. It's a list of features, not of looks. How a character looks is judged by eye against its sheets, and no row here says anything about that.

```bash
python reva_native/anim_bench/charcheck.py reva_native/packs/v1_reva902.rpack --against reva_native/packs/v1_<body>.rpack
```

It opens no window and poses nothing. `--against` lists the rows where the first body has less.

## What the bought character's licence lets us do

The bought character came from Fab, under its Standard License, and her listing says she may be used with AI. The listing and the agreement were read on 11-04, after I'd written that she could only ever be a reference. That was too strict. Read from the agreement itself (fab.com/eula, last updated October 1st 2024), by section:

| We may | Where it says so |
|---|---|
| Use her, copy her and change her however we like, in private | 3(a) |
| Take parts of her into our own characters: a hand, her eyes, her lashes, her teeth, her face shapes, her whole head. A project is anything made with the content, however much or little of it is used | 3(a), 4(a) |
| Publish films, takes and pictures made with her, freely | 4(b) |
| Ship games that have her in them, or anything made from her, to the public, ourselves or through a publisher | 4(c) |
| Keep her in a private repository, and share her with the people building the project with us | 5(a) |
| Use her alongside AI tools: she isn't tagged NoAI, and that rule is the only one about AI | 6(b)(vii), 16(l) |

| We may not | Where it says so |
|---|---|
| Put her, or anything that still contains her mesh, textures or shapes, in a public repository, or hand her to anyone who isn't building the project with us | 5(a) |
| Ship her as files a player can lift out and use elsewhere. In a game she's shipped as part of it, and players are to be kept from extracting her | 4(c) |
| Put her in a kit, a template or an editor that lets other people build her into their own work or export her | 6(b)(iii) |
| Sell or give her away on her own. What we ship has to add to her, with her as one part of it | 6(b)(ii) |
| Put her under a licence that would change her terms: GPL, LGPL, CC BY-SA. MIT code can sit beside her, but she can't be MIT herself | 6(a) |

What we make with her is ours, apart from her: the agreement keeps the content itself with its licensor (section 2, "Who Owns What"). So a REVA made by changing the bought character would be ours to ship in every game and film, and the mesh under her would still be the bought character's maker's.

**What that means here.**
- The hands on every body `skate_src/people/person.py` builds are the bought character's real hand, refitted (`extract_hand.py`). That's allowed in our games and films. It isn't allowed in this repository. Before step 28 publishes anything, those bodies either stay out of this repository or get a hand of our own.
- This repository can never carry the bought character, or a REVA built from her, and none of her is in it. It's exactly what 6(b)(iii) names. A character creator inside one of our games can use her parts; one that lets players export what they make can't.
- Our packs are a format of our own inside the game's folder, not encrypted. That's the usual way a game ships its content, and whether it's enough for 4(c)'s "restrict end users from extracting" is worth a look at step 28.
- REVA is rebuilt on the bought character's body: the choice is below. So REVA is one of the bodies this section is about.

## The list, and why each row is on it

| Row | Why the engine cares | The bought character |
|---|---|---|
| Eyes as meshes of their own | The eye setting makes them wet, a catch light sits in them, `eyes=` makes them glow, and a look can turn them. Paint on a head is skin to the engine. | 4, found by shape |
| Eye bones | A look that turns the eyes and not only the head. | 2, with a jaw, two rows of teeth and a three-bone tongue |
| A jaw | A mouth that opens when a line is said. | a bone and a shape |
| A blink | Nobody on screen stares. | yes |
| Face shapes | Expressions: a smile, a frown, the vowels. | 106 on her listing, 52 of them ARKit's. Our pack of her carries 26 |
| Lashes and brows | They're hair, and they frame the eye. | 3, by shape |
| Teeth and a tongue | An open mouth isn't a hole. | 3, by shape |
| Face marks | Where the eyes and the mouth are: the look, the talking lens and the conversation camera all read them. | 1 |
| Finger bones | Hands that hold, point and curl. | 34 |
| Hair as meshes, on chains | It swings, and it takes hair's long highlight. | 4 meshes, 198 thousand vertices, 15 chain bones |
| Detail in the head | A close-up holds. | 351 thousand vertices in the high-poly one we use. Her listing has a mid-poly one too, 129 thousand polygons in all |
| Normal maps | Skin and cloth aren't smooth plastic. | 2 |
| Soft bones | Flesh that moves when the body does. | 4 |

Where ours is ahead, and the bought character would have to come up to us:

| Row | Ours | The bought character |
|---|---|---|
| A wardrobe: pieces by slot, outfits, and skin cut away under what covers it | REVA 902: 27 pieces in 4 outfits, her skin in 32 parts | none: 18 loose garments that can't be changed |
| Cloth on chains, and skirts that hang as cloth | REVA: 24 chain bones. The show's cast: a simulated skirt each | none |
| True size | the show's cast, a built person, the animals | stood up to the engine's standard height |
| What each surface is, said in the pack | nobody yet: the tables and the eye finder do it by name and shape | the same |

## Where everyone stands (measured 11-04)

| | REVA 902 | a body of the show's cast | a person the game builds | The bought character |
|---|---|---|---|---|
| Eyes | paint on the head | paint on the face | balls in the one mesh | 4 meshes |
| Eye bones | 0 | 0 | 2 | 2 |
| A jaw | no | a shape | a bone | both |
| A blink | no | yes | yes | yes |
| Face shapes | 0 | 5 | 3 | 26 of her 106 |
| Lashes, brows, teeth, tongue | none | none | teeth, by name | all, by shape |
| Face marks | 0 | 0 | 7 | 1 |
| Finger bones | 30 | 30 | 0 | 34 |
| Hair | 3 meshes, 16 thousand vertices, 24 chain bones | 1 mesh, 28 chain bones | in the one mesh, 3 chain bones | 4 meshes, 15 chain bones |
| Vertices in the head | 17,502 | 7,993 | 144 | 351,256 |
| Wardrobe | 27 pieces, 4 outfits | 9 pieces, 2 outfits | none | none |
| Texture | one, 4096 square | one, 256 square | one, 128 by 64 | 31, to 2048 square |
| Normal maps | 0 | 0 | 0 | 2 |
| Clips of its own | 28 | 16 | 3 | 44 |

Read across: REVA has the body, the hands, the hair on chains and the wardrobe, and no face that works. She has no eyes the engine can find, no blink, no jaw, no face shapes and no face marks, so in a scene she can't look at anyone, blink or speak. The person a game builds is the other way round: a face that works, on a body of 2,576 vertices with mittens for hands and no clothes. The two builders each have what the other lacks.

## REVA's rebuild: on what

Two ways were open. On our own code every vertex is ours and she can go in this repository, and her face has to be modelled by hand in code, which is where she's weakest. On the bought character's body she has its face, eyes, lashes, teeth, face shapes and hands from the first day, she can ship in every game and film of ours and never in this repository, and the mesh under her stays the bought character's maker's.

The choice, 11-04: the bought character's body, with some adjustments, and the high-poly model, to work with on a PC. Then, with her holographic sheet (`sheet5.png`), that sheet's body type, and the rest below.

So, for REVA:

- **No words on her face.** The glowing 902 the sheets put on her forehead is out.
- **Her bare body is modelled first**, and her clothes are made for it afterwards, as wardrobe pieces.
- **C cups.** By the standard (EN 13402), which is what a shop's 32C or 34C means: bust 16 to 18 cm over the under-bust.
- **She never wears a bra.** No outfit of hers has one.
- **Her underwear is made with our own tools**, not taken from the bought character.

She's `props/v1_reva_hi.rpack`, made by her own builder (kept with her, not in this repository) from the bought character's pack, every time, with nothing done by hand. She is never committed. Her name is `reva_hi` because she's the high-poly body for a PC, and because `reva` is her September pack's name, which the menus and several scenes still use.

## A body's measurements

```bash
python reva_native/anim_bench/bodycheck.py reva_native/props/v1_reva_hi.rpack
```

It measures skin at rest the way a tailor does, with no window: heights off the floor, and girths as a tape pulled snug at one height. Measured 11-04:

| | The bought character | REVA |
|---|---|---|
| Stature | 178.7 cm | the same |
| Bust | 94.8 cm | 87.6 cm |
| Under the bust | 70.6 cm | 70.7 cm |
| Bust less under-bust | 24.2 cm | 16.9 cm |
| Cup, by the standard | 70G | 70C |
| Waist | 64.3 cm | the same |
| Hips | 99.1 cm | the same |
| Legs, of her height | 52.3% | the same |

Two things about cups. The letter is a difference between two tapes, so it's only as good as where the lower tape lies: right in the fold a breast's underside overhangs and the girth changes a centimetre for every millimetre of height, so the tool takes it on the ribs 6 mm under the fold. And there are two ways of lettering. The standard's C is a 16 to 18 cm difference. A newer UK fitting method, which a reference chart used, letters the same body about four sizes bigger, and its C is a 7.6 cm difference. The tool prints both.

## Her hips, against real women

11-04, with six more of her sheets: were her hips too wide, or the right size? Checked against anatomy standards.

```bash
python reva_native/anim_bench/bodycheck.py reva_native/props/v1_reva_hi.rpack --skin "<body>@1,reva_android,skin~h=briefs,reva_legs,skin~h=stockings,reva_arms,skin~h=top,reva_torso" --women
```

The standard is the U.S. Army's 2012 survey of its own people, which measured 1,986 women by hand (*2012 Anthropometric Survey of U.S. Army Personnel: Methods and Summary Statistics*, NATICK/TR-15/007; the tool's numbers are read from the report). The tool measures a body where the survey measures a woman: hips where the seat stands furthest back, the waist at the navel, shoulders across the upper arms with the arms down. "At their height" is her whole body brought down to those women's average height, which leaves her shape and takes out her size.

| | REVA | Those women, median | She's more than, as she is | At their height |
|---|---|---|---|---|
| Hip breadth | 35.3 cm | 35.3 cm | 50 in 100 | 12 in 100 |
| Hip girth | 99.1 cm | 101.8 cm | 36 in 100 | 6 in 100 |
| Thigh girth | 58.1 cm | 61.3 cm | 27 in 100 | 6 in 100 |
| Waist girth at the navel | 74.8 cm | 85.2 cm | 13 in 100 | 3 in 100 |
| Bust girth | 87.6 cm | 94.0 cm | 21 in 100 | 3 in 100 |
| Chest breadth | 24.8 cm | 26.8 cm | 12 in 100 | under 1 in 100 |
| Shoulders, arms down | 36.4 cm | 45.0 cm | under 1 in 100 | under 1 in 100 |
| Height | 178.5 cm | 162.6 cm | over 99 in 100 | |
| Crotch height | 93.3 cm | 78.0 cm | over 99 in 100 | 93 in 100 |

Her hips are a real woman's average, and narrow for a woman of her height. What's far from real women is her shoulders, narrower than 99 in 100 of theirs, and her legs, which are longer. So her hips are 0.97 of her shoulders across where those women's are 0.79, and that's what reads as wide hips. Her narrowest waist, 64.3 cm round, sits 6 cm above her navel and adds to it.

Her hips and thighs stay the bought character's. If her outline is ever to change, the numbers say her shoulders are the place, and that's a call made by eye.

### Her at that survey's average: seen, and not taken

11-05: how she would look with her anatomy set to that study's average.

Her builder made it with `--anatomy survey`, into `props/v1_reva_avg.rpack`.

`skate_src/author/anatomy.py` sets a rigged body's anatomy from a table of measurements, with nothing rebaked. Every bone gets a size of its own (a trunk bone a breadth, a height and a depth; a limb a length and a girth), a joint goes where its parent's size takes it, and a vertex goes where its own bones take it in the shares it already rides them by, so the skin stays one skin and every clip still plays. A fit moves the sizes until the body measures what the table says. Her head isn't touched. Her cup is put back by measuring, on the new ribs. Her limbs, hair and both outfits were then rebuilt on that body by the same script that builds her.

| | REVA | REVA at the survey's average |
|---|---|---|
| Stature, barefoot | 176.6 cm | 162.9 cm |
| Legs, of her height | 52.3% | 48.5% |
| Heads tall | 8.3 | 7.7 |
| Shoulders, arms down | 36.4 cm | 45.0 cm |
| Waist at the navel | 74.8 cm | 85.1 cm |
| Waist at its narrowest | 64.3 cm | 71.6 cm |
| Hips, round | 99.1 cm | 100.0 cm |
| Thigh, round | 58.2 cm | 61.6 cm |
| Bust over under-bust | 87.6 over 70.8: 70C | 94.7 over 77.7: 80C |

The two were compared side by side, in her base look and her skirt outfit, and the old version was preferred. So she stays as she is, hips and all, and the average body was deleted. The tool is the creator's: any table of the same measurements makes a body from hers.

Three things about the measuring. The survey's women were soldiers: fit, and mostly young. Her shoulders' number comes from turning her arms down in the tool, each skin point through its own bones, as the engine would. And her heels stand 2.5 cm off the floor, so flat on her feet she's a little shorter than 178.5 cm.

## REVA's rebuild, in order

From her reference sheets. Sheet 5, the holographic one, is THE reference, and the rest derive from it: where sheets disagree, it decides. Sheet 8 is the android in her skirt; 7 is the street look. Sheets 10 to 15 came 11-04, as other outfits and other derivations: 10 the mail assistant in a cropped jacket and strapped trousers, 11 her garden in a long gold-trimmed coat, 12 an outfit named piece by piece (a data-spike choker, a 902 crop, a longcoat, a tactical pleated skirt, one striped stocking and one fishnet, platform combat boots, a skull charm, the arm), 13 that coat over strapped trousers with its back's skull and her accessories close up, 14 her back in a brocade coat at her desk, 15 a vampire prince with a violin. They're other outfits on the same her. Each line is ticked when it's in her pack and a frame's been looked at.

1. [x] A face that works: eyes on eye bones, a blink, a jaw with teeth and a tongue, lashes and brows as hair. The bought character's, from the first day.
2. [x] Her face's paint: violet eyes, dark lashes, dark violet brows, plum lips, a smoky shadow with a wing. No words.
3. [x] Her bare body: everything of the bought character's that isn't her body or her hair is out of the pack.
4. [x] Her bust: a C, by measuring.
5. [x] What each of her surfaces is, said in her pack.
6. [x] Her underwear, made with our own tools and fitted to this body: `skate_src/author/skinwear.py` cuts tight clothes out of a body's own skin.
7. [x] What she wears in her first sheet, the same way: briefs, thigh-high stockings, garters, a band round her left thigh and arm, and her choker with its studs and ring, an important piece. Never a bra.
8. [x] She's an android: her left arm below its band and both legs from above the knee are bare machine, dark metal plates on a black core, with joints.
9. [x] Pink eyes.
10. [x] Her shoulder's mark: 902, on her left.
11. [ ] Her hair as her first sheet has it. Grown from her scalp as a violet shag with a fringe, three goes so far, none judged yet. It's rigid on her head.
12. [x] Sheet 8's outfit, as her second outfit, `skirt`: the pleated skirt, riding her hips and thighs by her skin's own weights, and its belt with pouches, buckle, ring and studs; the skull crop top; boots with thick soles and buckled straps. Not its chains yet. The soles can't be true platforms until an outfit can say how much taller it makes her. And the skirt isn't hung as cloth yet. The sheets across the room that stopped it were the renderer's and are fixed (11-05); what's left is that after a deep crouch the cloth stays up round her hips or comes down ragged, which is open (`engine/cloth.inc`).
13. [ ] More of her face's shapes: her pack carries 26 of the bought character's 106. Her own file with all of them is on disk since 11-04 (in the Fab library's cache).
14. [ ] The holographic look of her first sheet.

What isn't right yet and isn't hers alone: the underside of anything that faces the floor goes near black under a hard sun, because the engine works the ground's bounce out from the sky alone. Under each breast that reads as a dark line. It isn't in her skin.

## The creator

`skate_src/people/person.py` (a body from a tailor's measurements) and `engine/people.inc` (the same, while a game runs) are the character creator. It starts with REVA: her measurements are its first preset, and what her rebuild adds to a face, to hair and to hands goes into the builder, so the next person made has it too.

**Her preset is made (11-05).** One tool reads a finished body and says it as a person either builder can start from:

```bash
python skate_src/people/preset.py reva_native/props/v1_reva_hi.rpack --skin "<body>@1,reva_android,skin~h=briefs,reva_legs,skin~h=stockings,reva_arms,skin~h=top,reva_torso" --name reva
```

It prints her in `cast.py`'s words (stature, head, the landmark heights as shares of stature, hip, waist and chest as a half-width and two depths, limb radii and lengths), which is `cast.REVA` now, the first of `cast.PRESETS`. And it prints the dozen numbers of an `EngineBodySpec`, fitted with `people.inc`'s own tables, which is the first of `games/people`'s presets: the creator opens on her, with rows for her waist, her legs' share of her height and her eyes, her skin, her violet hair and her pink eyes among the colours, and START FROM to go back to her, to a plain grown-up or to a child.

| | REVA, measured | What the engine's spec builds |
|---|---|---|
| Stature | 178.5 cm | the same |
| Heads tall | 8.3 | the same |
| Legs, of her height | 52.3% | the same |
| Hips, across | 35.3 cm | 35.3 cm |
| Waist, round | 64.3 cm | 64.3 cm |
| Arm joints apart | 27.7 cm | 27.1 cm |
| Thigh, round | 58.1 cm | 58.1 cm |
| Hips, front to back | 22.7 cm | 20.6 cm |
| Chest, across | 24.8 cm | 28.0 cm |

The last two rows are what the engine's spec can't say. It has one `build` for the whole trunk, a width for hips and shoulders and a size for the waist: a narrow chest under her shoulders, and a seat deeper than her build, have no number. They'd be new fields on the end of `EngineBodySpec`. Her spine's curve has none in either builder: her rings' depths are shared fore and aft the way the builder's own body shares them.

It's her measurements, and no more of her than that: the face a builder makes is the builder's, and her own face, hands and hair are the bought character's and her pack's. The numbers carry nothing of the bought mesh, so they go in this repository with the creator.
