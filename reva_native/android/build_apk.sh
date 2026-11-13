#!/usr/bin/env bash
# build_apk.sh -- (11-06) a game as an Android APK, inside a container with the Android SDK and NDK and SDL2 built for Android (make_apk.py runs it).
# Mounts: /src the engine (reva_native, read only), /game the game's folder (read only), /sdl SDL2's Android prefix, /sdk the Android SDK, /work the
# output folder (the APK, the build's scratch), /keys the game's own signing key (made here the first time; never in git).
# Env: GAME (the game's folder name and .c), ABI (arm64-v8a or x86_64), STAMP (the data's version), PKG (the package), LABEL, VERSION_CODE, VERSION_NAME,
# ARGS (what the game starts with on the phone: "12 packs packs/v1_sample.rpack").
set -euo pipefail
GAME="${GAME:?}"; ABI="${ABI:-arm64-v8a}"; STAMP="${STAMP:?}"; VERSION_CODE="${VERSION_CODE:-1}"; VERSION_NAME="${VERSION_NAME:-0.1-test}"
PKG="${PKG:-com.example.$GAME}"; LABEL="${LABEL:-$GAME}"
NDK=/sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin
BT=/sdk/build-tools/36.1.0
PLAT=/sdk/platforms/android-36/android.jar
if [[ "$ABI" == x86_64 ]]; then CC=$NDK/x86_64-linux-android29-clang; else CC=$NDK/aarch64-linux-android29-clang; fi
W=/work/build_$ABI
rm -rf "$W"; mkdir -p "$W/lib/$ABI" "$W/classes" "$W/dex" "$W/res" "$W/assets"

echo "===== the engine and the game: libmain.so ($ABI)"
cd /src
"$CC" -O2 -std=gnu17 -ffp-contract=off -fPIC -shared -Wl,--no-undefined -Wno-comment -Dmain=SDL_main -DREVA_VK -DREVA_NODOORS -DREVA_SDL \
    -I/src -I/sdl/include -I/inc -o "$W/lib/$ABI/libmain.so" \
    crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c "games/$GAME/$GAME.c" gfx_packet.c gfx_vk.c \
    -L/sdl/lib -lSDL2 -lvulkan -llog -landroid -lm
cp /sdl/lib/libSDL2.so "$W/lib/$ABI/"
ls -la "$W/lib/$ABI"

echo "===== SDL2's Java: classes.dex"
if ! javac -nowarn -source 11 -target 11 -classpath "$PLAT" -d "$W/classes" $(find /src/android/sdl_java -name '*.java') > "$W/javac.log" 2>&1; then
    cat "$W/javac.log"; echo "javac failed"; exit 1; fi
"$BT/d8" --min-api 29 --lib "$PLAT" --output "$W/dex" $(find "$W/classes" -name '*.class')
ls -la "$W/dex"

echo "===== the data: assets"
cd /game
# what the game reads while it runs; never the tools, the builds, the tapes, the saves or the cook's map
find bodies lang music packs props scenes sets street tune words moves -type f 2>/dev/null > "$W/files.txt" || true
# and whatever else the game says it reads: android_extra.txt in its folder, a path a line
if [[ -f android_extra.txt ]]; then while read -r f; do f="${f%$'\r'}"; [[ -n "$f" && -f "$f" ]] && echo "$f" >> "$W/files.txt"; done < android_extra.txt; fi
sort -o "$W/files.txt" "$W/files.txt"
while read -r f; do mkdir -p "$W/assets/$(dirname "$f")"; cp "$f" "$W/assets/$f"; done < "$W/files.txt"
# what the game starts with on the phone, which engine/android.inc reads from its data
if [[ -n "${ARGS:-}" ]]; then printf '%s\n' "$ARGS" > "$W/assets/android_args.txt"; grep -qx android_args.txt "$W/files.txt" || echo android_args.txt >> "$W/files.txt"; fi
cp "$W/files.txt" "$W/assets/files.txt"
printf '%s' "$STAMP" > "$W/assets/apk_stamp.txt"
echo "$(wc -l < "$W/files.txt") files, $(du -sh "$W/assets" | cut -f1)"

echo "===== resources and manifest"
mkdir -p "$W/res/mipmap-mdpi" "$W/res/mipmap-hdpi" "$W/res/mipmap-xhdpi" "$W/res/mipmap-xxhdpi" "$W/res/mipmap-xxxhdpi"
for d in mdpi hdpi xhdpi xxhdpi xxxhdpi; do cp "/work/icon_$d.png" "$W/res/mipmap-$d/ic_launcher.png"; done
"$BT/aapt2" compile --dir "$W/res" -o "$W/res.zip"
# the manifest with this game's id and name in it (the file here carries placeholders)
sed -e "s|package=\"[^\"]*\"|package=\"$PKG\"|" -e "s|android:label=\"[^\"]*\"|android:label=\"$LABEL\"|g" /src/android/AndroidManifest.xml > "$W/AndroidManifest.xml"
"$BT/aapt2" link -o "$W/unsigned.apk" -I "$PLAT" --manifest "$W/AndroidManifest.xml" --min-sdk-version 29 --target-sdk-version 35 \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" --debug-mode -A "$W/assets" "$W/res.zip"
cd "$W"
cp dex/classes.dex .
zip -q -X unsigned.apk classes.dex
zip -q -X -r unsigned.apk lib
"$BT/zipalign" -p -f 4 unsigned.apk aligned.apk

echo "===== signed with the game's own key"
KS=/keys/${GAME}-test.keystore
if [[ ! -f "$KS" ]]; then
    PASS="$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 20)"
    keytool -genkeypair -keystore "$KS" -alias "$GAME" -keyalg RSA -keysize 4096 -validity 10000 -storepass "$PASS" -keypass "$PASS" \
        -dname "CN=${GAME} test build" >/dev/null 2>&1
    printf '%s' "$PASS" > "/keys/${GAME}-test.password"
    echo "made a new key for ${GAME} (its password beside it, in the keys folder: never in git)"
fi
PASS="$(cat /keys/${GAME}-test.password)"
OUT="/work/${GAME}-${VERSION_NAME}-${ABI}.apk"
"$BT/apksigner" sign --ks "$KS" --ks-key-alias "$GAME" --ks-pass "pass:$PASS" --key-pass "pass:$PASS" --out "$OUT" aligned.apk
"$BT/apksigner" verify "$OUT" && echo "verified"
ls -la "$OUT"
