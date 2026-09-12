#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/patched.h" "$OUT/dynamic_when_all.h"
cp "$SRC/tmp/rev/split.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O1 -I"$HOME/project/stdexec/include" split.cpp -o split -lpthread 2> err.split
echo "compile exit=$? errlines=$(wc -l < err.split)"
grep -m3 "error:" err.split | cut -c1-200
[ -x ./split ] && ./split
