#!/usr/bin/env bash
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/build/pool
mkdir -p "$OUT"

echo "===== 压 200 轮"
for CXX in clang++ g++; do
	bad=0
	for _ in $(seq 1 200); do
		"$OUT/pool.$CXX" > /dev/null 2>&1 || bad=$((bad + 1))
	done
	echo "$CXX : $((200 - bad))/200 通过"
done

echo
echo "===== ThreadSanitizer"
clang++ -std=c++2c -O1 -g -fsanitize=thread \
	-isystem "$HOME/project/stdexec/include" -I "$SRC/demo/new-arch" \
	"$SRC/tmp/rev/pool_test.cpp" -o "$OUT/pool.tsan" -lpthread 2> "$OUT/err.tsan"
echo "compile=$?"
grep -m3 "error:" "$OUT/err.tsan" | cut -c1-160
if [ -x "$OUT/pool.tsan" ]; then
	for round in 1 2 3; do
		if out=$(timeout 200 "$OUT/pool.tsan" 2>&1); then
			echo "第 $round 轮: 干净"
		else
			echo "第 $round 轮: 有问题"
			echo "$out" | grep -E "WARNING|SUMMARY" | head -4
		fi
	done
fi
