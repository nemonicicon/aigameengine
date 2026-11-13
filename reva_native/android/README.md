# A game on an Android phone (11-06)

```
python reva_native/android/make_apk.py --image IMAGE --sdk-volume SDKVOL --sdl-volume SDLVOL                          # the sample, arm64-v8a, a phone
python reva_native/android/make_apk.py --image IMAGE --sdk-volume SDKVOL --sdl-volume SDLVOL --game mygame --abi x86_64  # another game, the PC's emulator
```

`--image`, `--sdk-volume` and `--sdl-volume` can also be set as `ANDROID_IMAGE`, `ANDROID_SDK_VOLUME` and `ANDROID_SDL_VOLUME`; there's no default,
because only you know your container. It makes `reva_native/dist/android/<game>-<version>-<abi>.apk` (`--out` moves it), signed with the game's own
key in `<out>/keys/` (made the first time; never in git, never another project's). The app's id is `com.example.<game>` unless `--package <id>` says
otherwise, and its name is the game's.

- **What's in it:** the engine and the game compiled as `libmain.so` (the SDL road, `-DREVA_SDL`, with the NDK's clang, Android 10 and up), SDL2's
  `libSDL2.so` and its own Java (`sdl_java/`), the manifest here (`build_apk.sh` puts the game's id and name into it), and the game's data as the APK's
  assets: what the game reads while it runs, never its tools, builds, tapes, saves or the cook's map (`build_apk.sh` lists the folders, and a game's `android_extra.txt` adds single files, a path a line). What the game
  starts with goes in as `android_args.txt`, the words its launcher says after the exe: `--args`, or the game's own file of that name, or
  `package.py`'s table for the games here.
- **Where it's built:** `build_apk.sh` runs in a container, which no script here makes. It must hold:
  - the Android SDK in the volume `--sdk-volume`, mounted at `/sdk`, with the versions `build_apk.sh` names: NDK `29.0.14206865`, build-tools
    `36.1.0` (`aapt2`, `d8`, `zipalign`, `apksigner`) and the `android-36` platform;
  - SDL2 2.32.10 built for Android in the volume `--sdl-volume`, mounted at `/prefix`, one folder an ABI: `SDL2-2.32.10-android-arm64` and
    `SDL2-2.32.10-android-x86_64`, each with `include/` and `lib/libSDL2.so`. It must be the same version as `sdl_java/`;
  - in the image: a JDK (`javac` for Java 11, `keytool`), `zip`, `bash`.
  stb's headers are mounted from this machine: `--stb FOLDER` (or `ANDROID_STB`), which is MSYS2's `C:/msys64/mingw64/include/stb` on Windows
  unless it's given, and `/usr/include/stb` elsewhere (Debian's `libstb-dev`). ⛔ Only the Windows host has been tried.
- **On the phone** (`engine/android.inc`, all of it Android only): the first start says it's unpacking, then copies the data out of the APK into the
  app's own storage (a folder named for the APK's stamp, so a new APK's data replaces the old); the log is `files/log.txt` there (`adb shell run-as
  <package> cat files/log.txt`); touch controls (a stick wherever the left thumb lands, a look stick for the right thumb, the pad's buttons drawn on
  the right, MENU, and the phone's back button is Start); a crash or an exit before the game's loop shows a box with what happened and the log's last
  lines, the next start if it was a crash. A Bluetooth or USB pad works as on a PC (SDL).
- **The renderer on a phone** (`gfx_vk.c`, Android only): Vulkan 1.3's three calls and 1.2's query reset looked up on the device; RGBA, not BGRA (what Android's surfaces
  take); the swapchain's alpha and uses from what the surface says; the picture made the window's way up and turned by the compositor. A phone
  without Vulkan 1.3 (dynamic rendering) can't run it, and says so.
- **What was tested:** on 2026-11-13 these scripts built the sample game for both ABIs (under a minute each: compiled, linked, signed, the
  signature verified). ⛔ Neither APK has been started. On 2026-11-06, before the scripts took the container, the id and the game as arguments and
  before a game's start arguments came from its data, an APK of one game was started on an emulator (a Pixel 7 profile, Android 15): it unpacked,
  booted, drew full screen in landscape, and touch A and the stick worked. Nothing has been run on a real phone.

⛔ An APK carries the game's data: whoever builds it must have the right to ship that data.
