#!/bin/sh
set -eu

cd "$(dirname "$0")"
mkdir -p .sandbox/build .sandbox/tmp dist
export TMPDIR="$PWD/.sandbox/tmp"

host_cxx=${HOST_CXX:-clang++}
windows_cxx=${WINDOWS_CXX:-"$PWD/.sandbox/toolchain/llvm-mingw-20261117-ucrt-macos-universal/bin/i686-w64-mingw32-clang++"}
windows_windres=${WINDOWS_WINDRES:-"$(dirname "$windows_cxx")/i686-w64-mingw32-windres"}

"$host_cxx" -std=c++20 -Wall -Wextra -Werror -pedantic -g -Isrc \
    src/rules.cpp src/config.cpp tests/rules_test.cpp -o .sandbox/build/rules-test
.sandbox/build/rules-test

"$windows_cxx" -std=c++20 -O2 -Wall -Wextra -Werror -static -shared \
    src/rules.cpp src/config.cpp src/probe.cpp -o .sandbox/build/HeroLimit.Probe.dll
"$windows_cxx" -std=c++20 -O2 -Wall -Wextra -Werror -static -municode \
    tests/windows_probe.cpp -o .sandbox/build/HeroLimitProbe.exe
"$windows_cxx" -std=c++20 -O2 -Wall -Wextra -Werror -static -shared \
    src/rules.cpp src/config.cpp src/hota_runtime.cpp src/hota_runtime.def \
    -o .sandbox/build/HeroLim.dll

"$windows_windres" -I . -i src/installer.rc -o .sandbox/build/installer-res.o
"$windows_cxx" -std=c++20 -O2 -Wall -Wextra -Werror -static -mwindows -municode \
    src/installer.cpp .sandbox/build/installer-res.o -lbcrypt -lcomdlg32 -lshell32 \
    -o .sandbox/build/HeroLimitSetup.exe

cp .sandbox/build/HeroLimit.Probe.dll .sandbox/build/HeroLimitProbe.exe HeroLimit.ini dist/
archive=HeroLimit-HotA-1.8.1.zip
zip -q -X -j ".sandbox/build/$archive" .sandbox/build/HeroLimitSetup.exe INSTALL.txt
mv ".sandbox/build/$archive" dist/
manual=HeroLimit-HotA-1.8.1-manual.zip
zip -q -X -j ".sandbox/build/$manual" .sandbox/build/HeroLim.dll HeroLimit.ini \
    src/make_hero_limit_launcher.py MANUAL.txt INSTALL.txt
mv ".sandbox/build/$manual" dist/
