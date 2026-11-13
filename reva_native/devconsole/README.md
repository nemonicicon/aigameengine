# The dev console

Making a game with a Raspberry Pi 4 and your own AI subscription: the game on the TV, and a plain terminal to work from, on a second screen, over SSH, or on the same TV between plays. The AI runs wherever your subscription runs it; the Pi only needs its command-line tool. This folder works with any such tool.

*These scripts were written on 2026-11-03 and haven't been run on a fresh card yet, which is this phase's gate.*

## Getting started

On a fresh Raspberry Pi OS Lite, 64-bit, on a 4 GB Pi 4 or better, with this repo checked out:

```bash
sudo bash devconsole/setup_pi.sh --composite
```

`--composite` turns on the TV output and reboots into it. That turns HDMI off, because the Pi can't do both. Leave it out to keep HDMI. The script installs what the engine needs, builds it, and builds the sample game.

Then start your AI's tool inside the cap, from the repo's `reva_native` folder:

```bash
bash devconsole/agent.sh claude
```

Any command works in place of `claude`. Point it at `AGENTS.md`.

## On one TV

With no second screen and no SSH, the TV is the terminal, and an ordinary one is too wide and too small to read on a tube. The chatbox is a conversation with your AI in what a TV can show, in the console's biggest font, inside the safe box and following your screen fit:

```bash
python3 devconsole/chat.py claude -p --continue {}
```

Any command works: `{}` is your message, and with no `{}` the message goes to its stdin. Your tool has to be able to answer one message and exit, and remember the conversation itself, and it decides what it may do without asking. It runs in the same cap as `agent.sh`.

Type and press Enter. PgUp and PgDn read back, End comes back. Ctrl-C stops an answer. `/play games/yourgame 12 packs/your_hero.rpack` plays the game through `play.sh`, freezing the AI while you do, and what it printed comes back into the conversation. The game's whole log is `games/yourgame/last_play.log`, for your AI to read if something went wrong. `/play` on its own plays the same one again. The chat is its own program, so a game that crashes just brings you back to it. Everything said is also in `~/.aigame_chat.log`. Left alone for five minutes the screen goes dark, and any key brings it back.

It works over SSH or on any terminal too, at that terminal's size. `python3 devconsole/chat.py --selftest` checks it.

## Working on a game

Copy `games/sample` to `games/yourgame`, and rename what's inside. A game is one C file and `engine.h`, and `sample.c` is commented as a place to start.

```bash
GAME=games/yourgame/yourgame.c bash build_pi.sh dev
```

That builds `crowd_dev`, the engine with no game in it, which takes a few minutes and only happens when the engine changes. After that, `GAME=games/yourgame/yourgame.c bash build_pi.sh game` takes a second or two, and a running `crowd_dev` picks up the new game between ticks.

## Playing it

```bash
bash devconsole/play.sh games/yourgame 12 packs/your_hero.rpack
```

A game can only hold its frame rate with nothing else running, so this freezes the capped AI and anything it was compiling, runs the game on the TV, and thaws them when you quit. It prints how many frames were late.

## Seeing what it's doing

```bash
python3 devconsole/watch.py
```

One plain line a second: the tick, late frames, memory, temperature and whether the Pi is throttling. It works on any terminal, including over SSH.

For an AI to drive the game, start it with `REVA_BRIDGE=7766` and use `anim_bench/bridge.py`: `discover` lists what it can do. It can hold the clock, step it, press buttons, take a picture and read the hash, and a run driven this way replays exactly.

## What's checked

- `python3 anim_bench/hashcheck.py` says whether the game gives the same answer at any frame pacing, in seconds, with no display.
- `python3 anim_bench/packdump.py --check <folder>` says whether a pack is what `design/pack_format.md` says a pack is.
