#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
mkdir -p "$OUT"
cp "$SRC/tmp/rev/patched.h" "$OUT/dynamic_when_all.h"
cp "$SRC/demo/job-arch/manual_lifetime.h" "$SRC/tmp/rev/min.cpp" "$SRC/tmp/rev/smoke.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	for T in min smoke; do
		rm -f "$T.$CXX"
		$CXX -std=c++2c -Wall -Wextra -Wpedantic -O1 -I"$HOME/project/stdexec/include" \
			"$T.cpp" -o "$T.$CXX" -lpthread 2> "err.$T.$CXX"
		echo "-- $T exit=$? errlines=$(wc -l < "err.$T.$CXX")"
		grep -m3 -E "error:|warning:" "err.$T.$CXX" | cut -c1-180
		[ -x "$T.$CXX" ] && "./$T.$CXX"
	done
done
