#!/usr/bin/env bash
# consumer-arch/entity_arch.cpp 的一键构建 + 运行（WSL 侧）。
#
# 这个 TU 只依赖 stdexec + EnTT——Vulkan / SDL / vkfu / nagisa 都被内置的 mock_gpu 换掉了，
# 所以走不了 consumer-arch 的 CMakeLists（那边 find_package(Vulkan REQUIRED) 会先失败），
# 直接调编译器。同目录真正的 Vulkan demo 不受影响。
#
#   run-consumer-arch.sh              clang++ 构建并运行
#   run-consumer-arch.sh gcc          g++
#   run-consumer-arch.sh both         两个都来
#   run-consumer-arch.sh both stress  再各压 200 轮
set -uo pipefail

SRC=/mnt/c/Users/JDHC/projects/bleach_vs_naruto/demo
OUT=$HOME/build/consumer-arch
mkdir -p "$OUT"

build_and_run()
{
	local cxx=$1
	printf '\033[1m===== %s =====\033[0m\n' "$cxx"
	rm -f "$OUT/entity-arch.$cxx"
	$cxx -std=c++2c -Wall -Wextra -Wpedantic -Werror -O1 -g \
		-isystem "$HOME/project/stdexec/include" \
		-isystem "$HOME/project/entt/src" \
		"$SRC/consumer-arch/entity_arch.cpp" -o "$OUT/entity-arch.$cxx" -lpthread 2> "$OUT/err.$cxx"
	local rc=$?
	if [ "$rc" != 0 ]; then
		echo "编译失败:"
		grep -m6 -E "error:" "$OUT/err.$cxx" | cut -c1-200
		return 1
	fi
	"$OUT/entity-arch.$cxx"
}

stress()
{
	local cxx=$1 bad=0
	for _ in $(seq 1 200); do
		"$OUT/entity-arch.$cxx" > /dev/null 2>&1 || bad=$((bad + 1))
	done
	echo "$cxx : $((200 - bad))/200 通过"
}

case "${1:-clang}" in
	clang) compilers=(clang++) ;;
	gcc)   compilers=(g++) ;;
	both)  compilers=(clang++ g++) ;;
	*)     echo "用法: $0 [clang|gcc|both] [stress]" >&2; exit 2 ;;
esac

for cxx in "${compilers[@]}"; do
	build_and_run "$cxx" || exit 1
done

if [ "${2:-}" = "stress" ]; then
	echo
	echo "===== 压 200 轮 ====="
	for cxx in "${compilers[@]}"; do
		stress "$cxx"
	done
fi
