#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
HEADDIR=$HOME/stdexec-head
OUT=$HOME/rev-build
cp "$SRC/tmp/rev/semantics.cpp" "$OUT/"
cd "$OUT"
F=$HEADDIR/include/stdexec/__detail/__when_all.hpp

run() {
	clang++ -std=c++2b -O2 -isystem "$HEADDIR/include" semantics.cpp -o sem -lpthread 2> e.sem
	if [ $? -ne 0 ]; then echo "  compile FAILED: $(grep -m2 'error:' e.sem | cut -c1-160)"; return; fi
	timeout 120 ./sem
}

echo "=== 打了补丁的 HEAD"
run
echo
echo "=== 还原成原始 HEAD"
(cd "$HEADDIR" && git checkout -- "$F")
run
