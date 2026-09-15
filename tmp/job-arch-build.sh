#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto/demo/job-arch
OUT=$HOME/job-arch-build
mkdir -p "$OUT"
cp "$SRC"/*.h "$SRC"/main.cpp "$OUT/"
cd "$OUT"
for CXX in clang++ g++; do
	echo "########## $CXX"
	rm -f "demo.$CXX"
	$CXX -std=c++2c -Wall -Wextra -Wpedantic -Werror -O1 -g \
		-isystem "$HOME/project/stdexec/include" \
		-isystem "$HOME/project/entt/src" \
		main.cpp -o "demo.$CXX" -lpthread 2> "err.$CXX"
	rc=$?
	mine=$(grep -cE "^(\./)?(entities|node_sender|dynamic_when_all|manual_lifetime)\.h|^main\.cpp" "err.$CXX")
	echo "compile=$rc  自家文件里的诊断=$mine"
	if [ "$rc" != 0 ]; then grep -m6 -E "error:" "err.$CXX" | cut -c1-200; fi
	[ -x "demo.$CXX" ] && "./demo.$CXX"
done
