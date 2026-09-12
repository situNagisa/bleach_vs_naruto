#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/demo/job-arch/manual_lifetime.h" "$SRC/demo/job-arch/dynamic_when_all.h" \
   "$SRC/tmp/rev/probe_when_all.h" "$SRC/tmp/rev/race.cpp" "$OUT/"
cd "$OUT"
clang++ -std=c++2c -O2 -g -isystem "$HOME/project/stdexec/include" race.cpp -o race_real -lpthread 2> e1
echo "real   compile=$? $(grep -m1 'error:' e1 | cut -c1-140)"
clang++ -std=c++2c -O2 -g -DUSE_PROBE -isystem "$HOME/project/stdexec/include" race.cpp -o race_probe -lpthread 2> e2
echo "probe  compile=$? $(grep -m1 'error:' e2 | cut -c1-140)"
[ -x ./race_real ]  && timeout 90 ./race_real; echo "race_real exit=$?"
echo
[ -x ./race_probe ] && timeout 600 ./race_probe
