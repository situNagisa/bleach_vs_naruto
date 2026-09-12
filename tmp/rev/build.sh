#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
mkdir -p "$OUT"
cp "$SRC/demo/job-arch/dynamic_when_all.h" "$SRC/demo/job-arch/manual_lifetime.h" "$SRC/tmp/rev/smoke.cpp" "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "=============== $CXX"
	$CXX -std=c++2c -Wall -Wextra -Wpedantic -O1 -g \
		-I"$HOME/project/stdexec/include" \
		smoke.cpp -o "smoke.$CXX" -lpthread 2>&1 | head -60
	if [ -x "smoke.$CXX" ]; then echo "--- run:"; "./smoke.$CXX"; fi
done
