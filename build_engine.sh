#!/usr/bin/env bash
# Builds the portable C++ engine, its test runner and the benchmark with any C++20 compiler.
# Usage: Scripts/build_engine.sh [extra compiler flags]      (CXX overrides the compiler)
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-clang++}"
FLAGS="-std=c++20 -O2 -Wall -Wextra -IEngine/include $*"
case "$(uname -s)" in
  MINGW*|MSYS*) LINK="-static"; EXE=".exe" ;;
  Linux*) LINK="-pthread"; EXE="" ;;
  *) LINK=""; EXE="" ;;
esac
mkdir -p build
objs=()
for f in Engine/src/*.cpp; do
  o="build/$(basename "$f" .cpp).o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find Engine/include -newer "$o" -print -quit)" ]; then
    echo "compile $f"
    $CXX $FLAGS -c "$f" -o "$o"
  fi
  objs+=("$o")
done
echo "link sf_tests"
$CXX $FLAGS Engine/tests/test_*.cpp "${objs[@]}" $LINK -o "build/sf_tests$EXE"
if [ -f Engine/benchmarks/bench.cpp ]; then
  echo "link sf_bench"
  $CXX $FLAGS Engine/benchmarks/bench.cpp "${objs[@]}" $LINK -o "build/sf_bench$EXE"
fi
echo "done"
