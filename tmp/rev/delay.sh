#!/usr/bin/env bash
cd "$HOME" || exit 1
cp /mnt/c/Users/JDHC/projects/bleach_vs_naruto/tmp/rev/delay.cpp .
for CXX in clang++ g++; do
	echo "########## $CXX"
	if $CXX -std=c++2c -Wall -Wextra -Wpedantic -Werror -o "delay.$CXX" delay.cpp 2> "e.$CXX"; then
		"./delay.$CXX"
	else
		grep -m5 "error:" "e.$CXX" | cut -c1-170
	fi
done
