#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/patched.h" "$OUT/dynamic_when_all.h"
cp "$SRC/tmp/rev/split3.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O1 -I"$HOME/project/stdexec/include" split3.cpp -o split3 -lpthread 2> err.split3
echo "compile exit=$? errlines=$(wc -l < err.split3)"
grep -m5 "error:" err.split3 | cut -c1-200
[ -x ./split3 ] && ./split3
