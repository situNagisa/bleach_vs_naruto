#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto/demo/job-arch
OUT=$HOME/job-arch-cmake
rm -rf "$OUT" && mkdir -p "$OUT/src"
cp "$SRC"/*.h "$SRC"/main.cpp "$SRC"/CMakeLists.txt "$OUT/src/"
cd "$OUT"
cmake -S src -B build -G Ninja \
	-DCMAKE_CXX_COMPILER=clang++ \
	-DJOB_ARCH_STDEXEC_ROOT="$HOME/project/stdexec" \
	-DJOB_ARCH_ENTT_ROOT="$HOME/project/entt" 2>&1 | tail -12
echo "configure exit=$?"
cmake --build build 2>&1 | tail -8
echo "build exit=$?"
./build/job-arch | tail -4
