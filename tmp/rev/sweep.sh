#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/sweep.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2b -O2 -g -isystem "$HOME/project/stdexec/include" sweep.cpp -o sweep -lpthread 2> err.sweep
echo "compile=$? $(grep -m1 'error:' err.sweep | cut -c1-160)"
[ -x ./sweep ] && timeout 280 ./sweep 3000; echo "exit=$?"
