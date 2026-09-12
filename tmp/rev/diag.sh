#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/diag.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2b -O2 -g -isystem "$HOME/project/stdexec/include" diag.cpp -o diag -lpthread 2> err.diag
echo "compile=$? $(grep -m1 'error:' err.diag | cut -c1-200)"
[ -x ./diag ] && ./diag | c++filt -t
