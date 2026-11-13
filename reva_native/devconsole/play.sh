#!/bin/bash
# play.sh -- play the game on the TV, with nothing else running. usage: play.sh <game folder> <slots> <hero pack, relative to that folder>
# A frame rate can only be held with the agent and the compiler stopped, so this freezes the capped agent (and whatever it started), and thaws it when the game ends.
# From reva_native, after build_pi.sh dev. Quit from the game's start menu.
if [ "$#" -lt 3 ]; then echo "usage: play.sh <game folder> <slots> <hero pack>"; exit 1; fi
DIR="$1"; SLOTS="$2"; HERO="$3"
HERE="$(pwd)"
sudo systemctl freeze gameagent.scope 2>/dev/null && echo "[play] the agent is frozen"
LOG="$(mktemp)"
( cd "$DIR" && sudo env REVA_GAMELIB=./game.so REVA_MODE=9 "$HERE/crowd_dev" "$SLOTS" packs "$HERO" ) > "$LOG" 2>&1
CODE=$?
sudo systemctl thaw gameagent.scope 2>/dev/null && echo "[play] the agent is back"
grep -aE '^\[frames\]' "$LOG" | tail -1 | sed 's/.*| /[play] /'
grep -aE 'error|FAILED|cannot|refused' "$LOG" | head -5
cp "$LOG" "$DIR/last_play.log"   # what your AI needs when you tell it something went wrong
if [ "$CODE" != 0 ]; then echo "[play] the game stopped with exit $CODE. Its log is $DIR/last_play.log"; fi
rm -f "$LOG"
