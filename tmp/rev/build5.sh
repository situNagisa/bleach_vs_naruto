#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/patched.h" "$OUT/dynamic_when_all.h"
cp "$SRC/tmp/rev/split2.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O1 -I"$HOME/project/stdexec/include" split2.cpp -o split2 -lpthread 2> err.split2
echo "compile exit=$? errlines=$(wc -l < err.split2)"
grep -m3 "error:" err.split2 | cut -c1-200
[ -x ./split2 ] && ./split2
