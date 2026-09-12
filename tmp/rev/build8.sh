#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/demo/job-arch/dynamic_when_all.h" "$SRC/demo/job-arch/manual_lifetime.h" "$OUT/"
cp "$SRC/tmp/rev/letstopped.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O1 -isystem "$HOME/project/stdexec/include" letstopped.cpp -o letstopped -lpthread 2> err.ls
echo "compile exit=$? errlines=$(wc -l < err.ls)"
grep -m5 "error:" err.ls | cut -c1-200
[ -x ./letstopped ] && ./letstopped
