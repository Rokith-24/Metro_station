#!/usr/bin/env bash
set -euo pipefail

# Linux-first GoogleTest + GCOV runner for the Metro Management System.
# race_condition.c / race.c and deadlock.c are intentionally excluded.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ -f src/bst.c && -d include ]]; then
  SRC_DIR="src"
  INC_DIR="include"
  MAIN_FILE="src/main.c"
elif [[ -f bst.c && -f common.h ]]; then
  SRC_DIR="."
  INC_DIR="."
  if [[ -f main.c ]]; then MAIN_FILE="main.c"; elif [[ -f 'main(2).c' ]]; then MAIN_FILE='main(2).c'; else echo 'ERROR: Could not find main.c or main(2).c' >&2; exit 2; fi
else
  echo 'ERROR: Could not detect project layout. Expected src/ + include/ or flat C files.' >&2
  exit 2
fi

for f in bst.c storage.c auth.c audit_log.c station_validation_threads.c; do
  [[ -f "$SRC_DIR/$f" ]] || { echo "ERROR: Missing $SRC_DIR/$f" >&2; exit 2; }
done
[[ -f "$MAIN_FILE" ]] || { echo "ERROR: Missing $MAIN_FILE" >&2; exit 2; }
[[ -f test_gtest/test_metro_gtest.cpp ]] || { echo 'ERROR: Missing test_gtest/test_metro_gtest.cpp' >&2; exit 2; }

command -v gcc >/dev/null || { echo 'Install GCC first.' >&2; exit 2; }
command -v g++ >/dev/null || { echo 'Install G++ first.' >&2; exit 2; }
command -v gcov >/dev/null || { echo 'Install GCC coverage tools first.' >&2; exit 2; }

mkdir -p build/gtest
rm -f build/gtest/*
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O0 -g --coverage -pthread -I"$INC_DIR")
CXXFLAGS=(-std=c++17 -Wall -Wextra -Wpedantic -O0 -g --coverage -pthread -I"$INC_DIR")

# Compile C modules as C, then link with the C++ GoogleTest runner.
gcc "${CFLAGS[@]}" -c "$SRC_DIR/bst.c" -o build/gtest/bst.o
gcc "${CFLAGS[@]}" -c "$SRC_DIR/storage.c" -o build/gtest/storage.o
gcc "${CFLAGS[@]}" -c "$SRC_DIR/auth.c" -o build/gtest/auth.o
gcc "${CFLAGS[@]}" -c "$SRC_DIR/audit_log.c" -o build/gtest/audit_log.o
gcc "${CFLAGS[@]}" -c "$SRC_DIR/station_validation_threads.c" -o build/gtest/station_validation_threads.o
# Rename the app entry point so GoogleTest owns the real process main().
gcc "${CFLAGS[@]}" -Dmain=metro_app_main -c "$MAIN_FILE" -o build/gtest/main_test.o

g++ "${CXXFLAGS[@]}" test_gtest/test_metro_gtest.cpp \
  build/gtest/main_test.o build/gtest/bst.o build/gtest/storage.o \
  build/gtest/auth.o build/gtest/audit_log.o \
  build/gtest/station_validation_threads.o \
  -lgtest -pthread -lm -lstdc++fs -o build/gtest/metro_gtest

./build/gtest/metro_gtest

echo
echo '========== GCOV REPORT =========='
(
  cd build/gtest
  gcov -b -c main_test.gcno bst.gcno storage.gcno auth.gcno audit_log.gcno station_validation_threads.gcno
)

if command -v gcovr >/dev/null 2>&1; then
  echo
echo '========== AGGREGATE COVERAGE =========='
  # Keep the scope to project C modules, not GoogleTest framework code.
  gcovr --root "$ROOT" --filter '.*(main|bst|storage|auth|audit_log|station_validation_threads)\.c$' --print-summary --branches
  echo
echo 'To enforce 90% line coverage, rerun the gcovr command with --fail-under-line 90.'
else
  echo 'gcovr not found; install with: python3 -m pip install gcovr'
fi
