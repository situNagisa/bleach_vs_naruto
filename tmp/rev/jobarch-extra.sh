#!/bin/bash
set -u
SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto
OUT=$HOME/job-arch-build
cp "$SRC"/demo/job-arch/*.h "$OUT/"
cp "$SRC/tmp/rev/cycle.cpp" "$OUT/"
cd "$OUT"

echo "=== 环检测（应当 abort）"
clang++ -std=c++2c -Wall -Wextra -Wpedantic -Werror -O1 -g \
	-isystem "$HOME/project/stdexec/include" -isystem "$HOME/project/entt/src" \
	cycle.cpp -o cycle -lpthread 2> err.cycle
echo "compile=$? $(grep -m3 'error:' err.cycle | cut -c1-170)"
if [ -x ./cycle ]; then
	timeout 30 ./cycle 2>&1 | tail -3
	echo "exit=${PIPESTATUS[0]}  (134 = SIGABRT，即断言生效)"
fi

echo
echo "=== 主 demo 压 200 轮（错误 / 取消两条路径会 flake 的话这里看得出来）"
bad=0
for i in $(seq 1 200); do
	if ! ./demo.clang++ > /dev/null 2>&1; then bad=$((bad+1)); fi
done
echo "clang++ : $((200-bad))/200 通过"
bad=0
for i in $(seq 1 200); do
	if ! ./demo.g++ > /dev/null 2>&1; then bad=$((bad+1)); fi
done
echo "g++     : $((200-bad))/200 通过"
