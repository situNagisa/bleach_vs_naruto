#!/bin/bash
set -u
HEADDIR=$HOME/stdexec-head
OUT=$HOME/rev-build
F=$HEADDIR/include/stdexec/__detail/__when_all.hpp
cd "$HEADDIR" && git checkout -- "$F" 2>/dev/null

python3 - "$F" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
old = """        // Temporarily increment the count to avoid concurrent/recursive arrivals to
        // pull the rug under our feet. Relaxed memory order is fine here.
        __state_->__count_.fetch_add(1, __std::memory_order_relaxed);
"""
new = """        // Temporarily increment the count to avoid concurrent/recursive arrivals to
        // pull the rug under our feet. Only do so if the barrier has not already
        // been released: a plain fetch_add would take __count_ from 0 back to 1,
        // and this callback's own __arrive() would then see 1 and run __complete()
        // a second time, concurrently with the thread that released the barrier.
        // While this callback runs, that thread is blocked in ~inplace_stop_callback,
        // so the state is guaranteed to still be alive here.
        auto __old = __state_->__count_.load(__std::memory_order_relaxed);
        do
        {
          if (__old == 0)
          {
            return;
          }
        }
        while (!__state_->__count_.compare_exchange_weak(
          __old, __old + 1, __std::memory_order_acq_rel, __std::memory_order_relaxed));
"""
assert old in s, "anchor not found"
open(p, 'w', encoding='utf-8').write(s.replace(old, new))
print("patched", p)
PY

cd "$OUT"
for CXX in clang++ g++; do
	rm -f "fix.$CXX"
	$CXX -std=c++2b -O2 -g -isystem "$HEADDIR/include" repro.cpp -o "fix.$CXX" -lpthread 2> "ef.$CXX"
	echo "-- $CXX compile=$? $(grep -m1 'error:' ef.$CXX | cut -c1-160)"
	[ -x "fix.$CXX" ] && { timeout 200 "./fix.$CXX"; echo "   exit=$?"; }
done
