#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/demo/job-arch/manual_lifetime.h" "$SRC/tmp/rev/probe_when_all.h" "$SRC/tmp/rev/race.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O2 -g -isystem "$HOME/project/stdexec/include" race.cpp -o race -lpthread 2> err.race
echo "compile exit=$? errlines=$(wc -l < err.race)"
grep -m5 "error:" err.race | cut -c1-200
[ -x ./race ] && timeout 300 ./race; echo "run exit=$?"
