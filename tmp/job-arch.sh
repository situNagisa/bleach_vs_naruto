#!/usr/bin/env bash
# job-arch demo 的一键构建 + 运行（WSL 侧）。
#
# 源码留在 Windows 侧直接读，改完存盘就能重跑；构建目录放在 WSL 文件系统里，
# 因为 /mnt/c 的 IO 慢，而且 CMake 的增量判断在跨文件系统时不稳。
#
#   run-job-arch.sh              clang++ 构建并运行
#   run-job-arch.sh gcc          g++ 构建并运行
#   run-job-arch.sh both         两个编译器都来一遍
#   run-job-arch.sh clang clean  先删构建目录再来
#
# 依赖（已在本机装好）：
#   stdexec  ~/project/stdexec      （pin 在 f91f6363）
#   EnTT     ~/project/entt         （只有 src/，header-only 够用）
set -euo pipefail

SOURCE_DIR=/mnt/c/Users/JDHC/projects/bleach_vs_naruto/demo/job-arch
STDEXEC_ROOT=$HOME/project/stdexec
ENTT_ROOT=$HOME/project/entt

compiler=${1:-clang}
if [ "${2:-}" = "clean" ]; then
	rm -rf "$HOME/build/job-arch-clang" "$HOME/build/job-arch-gcc"
fi

build_and_run()
{
	local name=$1 cxx=$2
	local build_dir="$HOME/build/job-arch-$name"

	if [ ! -f "$build_dir/CMakeCache.txt" ]; then
		cmake -S "$SOURCE_DIR" -B "$build_dir" -G Ninja \
			-DCMAKE_BUILD_TYPE=RelWithDebInfo \
			-DCMAKE_CXX_COMPILER="$cxx" \
			-DJOB_ARCH_STDEXEC_ROOT="$STDEXEC_ROOT" \
			-DJOB_ARCH_ENTT_ROOT="$ENTT_ROOT" > /dev/null
	fi

	printf '\033[1m===== %s =====\033[0m\n' "$cxx"
	cmake --build "$build_dir" | grep -vE '^\[[0-9]+/[0-9]+\] (Building|Linking)' || true
	"$build_dir/job-arch"
}

case "$compiler" in
	clang) build_and_run clang clang++ ;;
	gcc)   build_and_run gcc   g++ ;;
	both)  build_and_run clang clang++; echo; build_and_run gcc g++ ;;
	*)     echo "用法: $0 [clang|gcc|both] [clean]" >&2; exit 2 ;;
esac
