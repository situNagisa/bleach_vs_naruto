#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
mkdir -p "$OUT"
cp "$SRC/demo/job-arch/dynamic_when_all.h" "$SRC/demo/job-arch/manual_lifetime.h" "$OUT/"
cp "$SRC/tmp/rev/min.cpp" "$SRC/tmp/rev/smoke.cpp" "$SRC/tmp/rev/split3.cpp" "$SRC/tmp/rev/sizes.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	for T in min smoke split3 sizes; do
		rm -f "$T.$CXX"
		$CXX -std=c++2c -Wall -Wextra -Wpedantic -Werror -O1 \
			-isystem "$HOME/project/stdexec/include" "$T.cpp" -o "$T.$CXX" -lpthread 2> "err.$T.$CXX"
		rc=$?
		mine=$(grep -cE "^(\./)?(dynamic_when_all|manual_lifetime)\.h" "err.$T.$CXX")
		echo "-- $T exit=$rc  our-header-diagnostics=$mine"
		[ "$rc" != 0 ] && grep -m6 -E "error:" "err.$T.$CXX" | cut -c1-180
		[ -x "$T.$CXX" ] && "./$T.$CXX"
	done
done
