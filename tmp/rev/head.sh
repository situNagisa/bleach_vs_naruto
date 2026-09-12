#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/upstream_when_all_repro.cpp" "$OUT/repro.cpp"
cd "$OUT"
echo "=== stdexec HEAD: $(cd $HOME/stdexec-head && git log -1 --format='%h %ad' --date=short)"
for CXX in clang++ g++; do
	rm -f "head.$CXX"
	$CXX -std=c++2b -O2 -g -isystem "$HOME/stdexec-head/include" repro.cpp -o "head.$CXX" -lpthread 2> "eh.$CXX"
	echo "-- $CXX compile=$? $(grep -m1 'error:' eh.$CXX | cut -c1-160)"
	if [ -x "head.$CXX" ]; then timeout 60 "./head.$CXX"; echo "   exit=$?"; fi
done
