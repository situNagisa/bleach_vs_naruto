#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/upstream_when_all_repro.cpp" "$OUT/repro.cpp"
cd "$OUT"
echo "=== pinned revision: $(cd $HOME/project/stdexec && git log -1 --format='%h %ad' --date=short)"
for CXX in clang++ g++; do
	rm -f "repro.$CXX"
	$CXX -std=c++2b -O2 -g -isystem "$HOME/project/stdexec/include" repro.cpp -o "repro.$CXX" -lpthread 2> "err.$CXX"
	echo "-- $CXX compile=$? $(grep -m1 'error:' err.$CXX | cut -c1-160)"
	if [ -x "repro.$CXX" ]; then timeout 120 "./repro.$CXX"; echo "   exit=$?"; fi
done
