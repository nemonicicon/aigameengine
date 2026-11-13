#!/bin/bash
# make_image.sh -- A PI IMAGE: a card that boots into a game (queue step 28). It takes a stock Raspberry Pi OS Lite (64-bit) image and a game packaged for the Pi
# (python reva_native/package.py --game <name> --target pi) and writes a new image with the game on it and a unit that starts it on the console at boot.
#
#   sudo ./make_image.sh --base raspios-lite-arm64.img --package sample-1.0-pi4-arm64.tar.gz --out club-sample.img [--composite] [--debs DIR]
#   ./make_image.sh --root DIR --boot DIR --package PKG.tar.gz [--composite] [--debs DIR]     the same files onto two folders: a mounted card, or a test
#
#   --composite   the TV output: dtoverlay=vc4-kms-v3d,composite and enable_tvout=1 in config.txt. This turns HDMI off; the Pi can't do both.
#   --debs DIR    .deb files to install on the first boot with no network (libsdl2-2.0-0, libvulkan1, mesa-vulkan-drivers, libdrm2 and what they need:
#                 on a Pi with a network, `apt-get download` fetches them). Without it the first boot installs them from the network, and waits for one.
#
# What it puts on the card: /opt/club/<game>/ (the package, as it is), club-firstboot.service (once: a user called club with no password and no login, in the
# groups that reach the screen, the pads and the sound; then the libraries), club-game.service (the game, on the console, started again if it stops), and the
# config.txt lines. It keeps config.txt as it was beside itself. It sets no password and makes no login: the base image's own first-boot does that.
#
# ⛔ WRITTEN 11-05 AND NOT RUN ON A CARD. The half that lays the files down was run onto two empty folders and looked at (the same code the image half calls).
# The image half (losetup, mount) needs root on Linux and a base image, and no card has been booted from what it writes: the fresh-card test is still to do.
set -e
BASE=; PKG=; OUT=; ROOT=; BOOT=; COMPOSITE=0; DEBS=
while [ $# -gt 0 ]; do case "$1" in
  --base) BASE=$2; shift 2;; --package) PKG=$2; shift 2;; --out) OUT=$2; shift 2;; --root) ROOT=$2; shift 2;; --boot) BOOT=$2; shift 2;;
  --composite) COMPOSITE=1; shift;; --debs) DEBS=$2; shift 2;;
  *) echo "make_image.sh: what's $1? (--base --package --out, or --root --boot --package; --composite; --debs DIR)"; exit 2;; esac; done
[ -f "$PKG" ] || { echo "no package at '$PKG': python reva_native/package.py --game <name> --target pi makes one"; exit 1; }

lay() {   # $1 the card's root, $2 its boot partition
  local R=$1 B=$2 TOP GAME
  TOP=$(tar tzf "$PKG" | head -1 | cut -d/ -f1)
  GAME=${TOP%%-*}
  [ -n "$GAME" ] || { echo "can't tell the game's name from $PKG"; exit 1; }
  mkdir -p "$R/opt/club" "$R/etc/systemd/system/multi-user.target.wants"
  rm -rf "$R/opt/club/$GAME"
  tar xzf "$PKG" -C "$R/opt/club"
  mv "$R/opt/club/$TOP" "$R/opt/club/$GAME"
  [ -x "$R/opt/club/$GAME/$GAME" ] || { echo "the package has no $GAME in it that can be run"; exit 1; }
  if [ -n "$DEBS" ]; then mkdir -p "$R/opt/club/debs"; cp "$DEBS"/*.deb "$R/opt/club/debs/"; fi
  cat > "$R/opt/club/first_boot.sh" <<'EOF'
#!/bin/sh
# once, on the card's first boot: who the game runs as, and the libraries it asks for. It leaves a mark, and club-firstboot.service doesn't run again
set -e
id club >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin --groups video,render,input,audio club
if ls /opt/club/debs/*.deb >/dev/null 2>&1; then dpkg -i /opt/club/debs/*.deb
else
  n=0; until getent hosts deb.debian.org >/dev/null 2>&1 || [ $n -ge 60 ]; do sleep 2; n=$((n+1)); done
  apt-get update && apt-get install -y --no-install-recommends libsdl2-2.0-0 libvulkan1 mesa-vulkan-drivers libdrm2
fi
chown -R club:club /opt/club
touch /opt/club/.ready
EOF
  chmod 755 "$R/opt/club/first_boot.sh"
  cat > "$R/etc/systemd/system/club-firstboot.service" <<EOF
[Unit]
Description=The console's first boot: its user and its libraries
ConditionPathExists=!/opt/club/.ready
Before=club-game.service
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
ExecStart=/opt/club/first_boot.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
  cat > "$R/etc/systemd/system/club-game.service" <<EOF
[Unit]
Description=$GAME, on the console
ConditionPathExists=/opt/club/.ready
After=club-firstboot.service systemd-user-sessions.service sound.target
Conflicts=getty@tty1.service
After=getty@tty1.service

[Service]
User=club
SupplementaryGroups=video render input audio
WorkingDirectory=/opt/club/$GAME
ExecStart=/opt/club/$GAME/play.sh
Restart=on-failure
RestartSec=2
TTYPath=/dev/tty1
StandardInput=tty
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
  ln -sf ../club-firstboot.service "$R/etc/systemd/system/multi-user.target.wants/club-firstboot.service"
  ln -sf ../club-game.service "$R/etc/systemd/system/multi-user.target.wants/club-game.service"
  local CFG="$B/config.txt"
  if [ -f "$CFG" ]; then
    [ -f "$CFG.before_club" ] || cp "$CFG" "$CFG.before_club"
    if [ "$COMPOSITE" = 1 ]; then
      if grep -q '^dtoverlay=vc4-kms-v3d' "$CFG"; then sed -i 's/^dtoverlay=vc4-kms-v3d.*/dtoverlay=vc4-kms-v3d,composite/' "$CFG"; else echo 'dtoverlay=vc4-kms-v3d,composite' >> "$CFG"; fi
      grep -q '^enable_tvout=1' "$CFG" || echo 'enable_tvout=1' >> "$CFG"
    else
      grep -q '^dtoverlay=vc4-kms-v3d' "$CFG" || echo 'dtoverlay=vc4-kms-v3d' >> "$CFG"
    fi
  else echo "no config.txt in $B: the display's lines aren't written"; fi
  echo "laid down: /opt/club/$GAME ($(find "$R/opt/club/$GAME" -type f | wc -l) files), club-firstboot.service, club-game.service$([ "$COMPOSITE" = 1 ] && echo ', the TV output in config.txt')"
}

if [ -n "$ROOT" ]; then
  [ -n "$BOOT" ] || { echo "--root wants --boot too"; exit 2; }
  lay "$ROOT" "$BOOT"; exit 0
fi
[ -f "$BASE" ] && [ -n "$OUT" ] || { echo "the image half wants --base <a Raspberry Pi OS Lite 64-bit image> and --out <the image to write>"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "the image half mounts the image: run it with sudo, on Linux"; exit 1; }
case "$BASE" in *.xz) xz -dc "$BASE" > "$OUT";; *) cp "$BASE" "$OUT";; esac
LOOP=$(losetup -Pf --show "$OUT"); MNT=$(mktemp -d)
trap 'umount "$MNT/boot" "$MNT/root" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null; rmdir "$MNT/boot" "$MNT/root" "$MNT" 2>/dev/null' EXIT
mkdir -p "$MNT/root" "$MNT/boot"
mount "${LOOP}p2" "$MNT/root"; mount "${LOOP}p1" "$MNT/boot"
lay "$MNT/root" "$MNT/boot"
sync
echo "wrote $OUT. Flash it to a card, and the first boot sets it up. ⛔ No card has been booted from one of these yet."
