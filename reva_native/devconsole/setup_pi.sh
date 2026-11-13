#!/bin/bash
# setup_pi.sh -- a fresh Raspberry Pi OS Lite (64-bit) into a dev console: what the engine needs, the engine, the sample game. Run with sudo from a checkout of this repo.
#   --composite   turn on the TV output and reboot into it. This turns HDMI off: the Pi can't do both (design/engine_plan.md, the parked ruling on HDMI and composite)
# It changes config.txt only when asked to, and keeps a copy of what was there.
set -e
cd "$(dirname "$0")/.."
if [ "$(id -u)" != 0 ]; then echo "run this with sudo"; exit 1; fi
if [ "$(uname -m)" != aarch64 ]; then echo "this is for a 64-bit Pi OS: the packs are read through packed structs of floats, which 32-bit VFP won't load"; exit 1; fi
apt-get update
apt-get install -y build-essential git libdrm-dev libsdl2-dev libstb-dev libvulkan-dev mesa-vulkan-drivers vulkan-tools python3 python3-numpy python3-pil kbd console-setup
vulkaninfo --summary 2>/dev/null | grep -E 'apiVersion|deviceName' || echo "vulkaninfo found no GPU: check dtoverlay=vc4-kms-v3d in config.txt"
CFG=/boot/firmware/config.txt
if [ "$1" = --composite ] && [ -f "$CFG" ]; then
  cp -n "$CFG" "$CFG.before_devconsole"
  grep -q '^dtoverlay=vc4-kms-v3d' "$CFG" || echo 'dtoverlay=vc4-kms-v3d,composite' >> "$CFG"
  grep -q 'vc4-kms-v3d,composite' "$CFG" || sed -i 's/^dtoverlay=vc4-kms-v3d.*/dtoverlay=vc4-kms-v3d,composite/' "$CFG"
  grep -q '^enable_tvout=1' "$CFG" || echo 'enable_tvout=1' >> "$CFG"
fi
U="${SUDO_USER:-root}"
usermod -aG input,video,render,audio "$U"   # the pads, the screen, the GPU, the sound
sudo -u "$U" env GAME=games/sample/sample.c bash build_pi.sh dev
sudo -u "$U" env GAME=games/sample/sample.c EXTRA=-DREVA_NODOORS bash build_pi.sh headless
sudo -u "$U" python3 anim_bench/hashcheck.py --game sample
echo
echo "The sample game ran on this Pi, and gave the same answer at every frame pacing."
echo "Next: python3 devconsole/chat.py <your AI's command> {} on the TV, or bash devconsole/agent.sh <your AI's command> over SSH, and devconsole/README.md for the rest."
if [ "$1" = --composite ]; then echo "Rebooting into the TV output in 10 seconds."; sleep 10; reboot; fi
