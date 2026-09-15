#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/entt_probe.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	rm -f "probe.$CXX"
	$CXX -std=c++2c -Wall -Wextra -Wpedantic -O1 -isystem "$HOME/project/entt/src" \
		entt_probe.cpp -o "probe.$CXX" 2> "ee.$CXX"
	echo "compile=$? $(grep -m2 'error:' ee.$CXX | cut -c1-170)"
	[ -x "probe.$CXX" ] && "./probe.$CXX"
done
