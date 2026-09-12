#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
mkdir -p "$OUT"
cp "$SRC/demo/job-arch/dynamic_when_all.h" "$SRC/demo/job-arch/manual_lifetime.h" "$SRC/tmp/rev/min.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	rm -f "min.$CXX"
	$CXX -std=c++2c -Wall -Wextra -Wpedantic -O1 -I"$HOME/project/stdexec/include" \
		min.cpp -o "min.$CXX" -lpthread 2> "err.$CXX"
	rc=$?
	echo "exit=$rc  errlines=$(wc -l < "err.$CXX")"
	grep -m4 -E "error:" "err.$CXX" | cut -c1-220
	[ -x "min.$CXX" ] && { echo "-- run:"; "./min.$CXX"; }
done
