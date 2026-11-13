#!/bin/sh
# Builds the solver's test, its bench and the Godot extension with the machine's own C compiler, on a Mac or on Linux. A library that fails the test or the bench isn't copied anywhere.
#   sh build_unix.sh [folder for the library]
set -e
to=""
if [ -n "$1" ]; then
    to=$(cd "$1" && pwd)
fi
cd "$(dirname "$0")"
mkdir -p out
cc --version | head -n 1
cc -O2 -fno-math-errno test_clubcloth.c clubcloth.c -lm -o out/test_clubcloth
out/test_clubcloth > out/test.txt || { cat out/test.txt; exit 1; }
tail -n 1 out/test.txt
# Does it move like cloth? The bench holds it to physics and the textile lab's numbers.
cc -O2 -fno-math-errno bench_clubcloth.c clubcloth.c -lm -o out/bench_clubcloth
out/bench_clubcloth > out/bench.txt || { cat out/bench.txt; exit 1; }
grep "all passed" out/bench.txt
# No -ffast-math for the library: gcc's sets how the whole process treats tiny numbers, Godot and all, when it's loaded.
case "$(uname -s)" in
Darwin)
    lib=club_cloth.macos.universal.dylib
    # Both kinds of Mac, in one file. A Mac won't run an arm64 library with no signature, and an ad hoc one is all Godot's editor asks for.
    cc -O2 -fno-math-errno -dynamiclib -arch arm64 -arch x86_64 -mmacosx-version-min=10.15 -install_name "@rpath/$lib" -fvisibility=hidden -Igodot -I. godot/club_cloth_gde.c clubcloth.c -o "out/$lib"
    codesign --force --sign - "out/$lib"
    ;;
*)
    lib=club_cloth.linux.x86_64.so
    cc -O2 -fno-math-errno -shared -fPIC -fvisibility=hidden -Igodot -I. godot/club_cloth_gde.c clubcloth.c -lm -lpthread -o "out/$lib"
    ;;
esac
if [ -n "$to" ]; then
    cp "out/$lib" "$to/"
fi
echo "built out/$lib, $(wc -c < "out/$lib") bytes"
