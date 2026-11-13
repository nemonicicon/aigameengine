# SDL2's Android Java

`org/libsdl/app/*.java` is SDL 2.32.10's own Java for Android (`android-project/app/src/main/java/org/libsdl/app/` in SDL's source), under SDL's
zlib licence (Copyright (C) 1997-2025 Sam Lantinga). It starts the engine on a phone: `SDLActivity` loads `libSDL2.so` and `libmain.so` and calls
`SDL_main`. It must be the same version as the `libSDL2.so` the APK carries (SDLActivity checks). Taken 11-06 from a copy another project had, with that
project's three calls into its own activity taken out of `SDLSurface.java`, so it's SDL's as released.
