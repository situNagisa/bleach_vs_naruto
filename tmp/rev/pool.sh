#!/usr/bin/env bash
# resource_pool 的行为测试。只依赖 stdexec。
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/build/pool
mkdir -p "$OUT"
for CXX in clang++ g++; do
	printf '\033[1m===== %s =====\033[0m\n' "$CXX"
	rm -f "$OUT/pool.$CXX"
	$CXX -std=c++2c -Wall -Wextra -Wpedantic -Werror -O1 -g \
		-isystem "$HOME/project/stdexec/include" \
		-I "$SRC/demo/new-arch" \
		"$SRC/tmp/rev/pool_test.cpp" -o "$OUT/pool.$CXX" -lpthread 2> "$OUT/err.$CXX"
	rc=$?
	mine=$(grep -cE "pool_test\.cpp|resource_pool\.h" "$OUT/err.$CXX")
	echo "compile=$rc  自家文件里的诊断=$mine"
	if [ "$rc" != 0 ]; then
		grep -m6 -E "error:" "$OUT/err.$CXX" | cut -c1-200
	fi
	[ -x "$OUT/pool.$CXX" ] && timeout 60 "$OUT/pool.$CXX"
done
