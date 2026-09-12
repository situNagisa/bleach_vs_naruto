#!/bin/bash
set -u
OUT=$HOME/rev-build
cd "$OUT"
# 看门狗改成：挂住 5 秒后自己 raise(SIGABRT)，在 gdb 里就能停下来抓全部线程栈
sed -e 's/std::_Exit(2);/std::abort();/' repro.cpp > repro_trap.cpp
grep -q 'std::abort' repro_trap.cpp || { echo "sed 没改到"; exit 1; }
clang++ -std=c++2b -O1 -g -fno-omit-frame-pointer -isystem "$HOME/stdexec-head/include" \
	repro_trap.cpp -o repro_trap -lpthread 2> e.trap
echo "compile=$? $(grep -m1 'error:' e.trap | cut -c1-160)"
timeout 120 gdb -batch -nx -ex run -ex "thread apply all bt 12" ./repro_trap 2>&1 \
	| grep -E "^Thread|^#[0-9]|DEADLOCK|round .*completed" \
	| sed -E 's/\(.*\)//; s/ at \/home[^ ]*\// at /' | head -70
