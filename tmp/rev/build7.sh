#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/patched2.h" "$OUT/dynamic_when_all.h"
cp "$SRC/demo/job-arch/manual_lifetime.h" "$OUT/"
cp "$SRC/tmp/rev/min.cpp" "$SRC/tmp/rev/smoke.cpp" "$SRC/tmp/rev/split3.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	for T in min smoke split3; do
		rm -f "$T.$CXX"
		$CXX -std=c++2c -Wall -Wextra -Wpedantic -O1 -I"$HOME/project/stdexec/include" \
			"$T.cpp" -o "$T.$CXX" -lpthread 2> "err.$T.$CXX"
		rc=$?
		own=$(grep -c "dynamic_when_all.h\|manual_lifetime.h" "err.$T.$CXX")
		echo "-- $T exit=$rc  diagnostics-in-our-headers=$own"
		grep -m3 "error:" "err.$T.$CXX" | cut -c1-170
		[ -x "$T.$CXX" ] && "./$T.$CXX"
	done
done
