#!/bin/sh
# Everything that can be checked without a GPU, in the order a failure is
# cheapest to read: the generator's own tests, then the naming table it produces
# against the real registry, then the C++ that comes out the other end.
#
#     tools/tests/run.sh            # generator only
#     tools/tests/run.sh --cpp      # also compile the fixture consumer
set -e

cd "$(dirname "$0")/.."

echo "== generator unit tests =="
python -m unittest discover -s tests -t . 2>&1 | tail -5

echo
echo "== naming table against the real registry =="
python -m vkfu_gen check --xml vk.xml --table naming.toml --scope closure \
	| tail -1

echo
echo "== the table is reproducible =="
python -m vkfu_gen rebuild --xml vk.xml --table /tmp/vkfu-rebuilt.toml \
	--overrides naming.overrides.toml >/dev/null 2>&1
if diff -q naming.toml /tmp/vkfu-rebuilt.toml >/dev/null; then
	echo "naming.toml matches what rebuild produces"
else
	echo "naming.toml differs from rebuild -- run rebuild and review the diff" >&2
	exit 1
fi

if [ "$1" != "--cpp" ]; then
	exit 0
fi

echo
echo "== the fixture's generated header is real C++ =="
for cc in g++ clang++; do
	command -v "$cc" >/dev/null || continue
	"$cc" -std=c++23 -Wall -Wextra -Werror -O1 -I tests -I vulkan-headers \
		-o /tmp/vkfu-fixture tests/fixture_consumer.cpp
	/tmp/vkfu-fixture
	echo "$cc: ok"
done
