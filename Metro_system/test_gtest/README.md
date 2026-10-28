# Metro Management System — GoogleTest + GCOV (Linux)

This package contains a GoogleTest C++ harness for the supplied C modules. It intentionally excludes `race_condition.c` / `race.c` and `deadlock.c`, as requested.

## Included files

- `test_metro_gtest.cpp` — API tests plus an integration test that runs the application's real entry point with scripted stdin.
- `run_coverage_linux.sh` — Linux build/test/GCOV script; detects either the usual `src/` + `include/` layout or a flat source layout.
- `README.md` — instructions and scope notes.

## Prerequisites (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install build-essential g++ cmake libgtest-dev gcovr
```

If `libgtest-dev` is installed but the linker cannot find `-lgtest`, consult your distribution's GoogleTest packaging instructions or build/install GoogleTest from its official source.

## Run

Place the `test_gtest` directory in the project root. Ensure the project source/header files are in either:

- `src/` and `include/` (usual layout), or
- the project root (flat layout).

Then run from the project root:

```bash
bash test_gtest/run_coverage_linux.sh
```

The script compiles each C module as C with `--coverage`, renames the app's `main()` to `metro_app_main`, links GoogleTest, runs the tests, and prints GCOV line/branch information. `race_condition.c` and `deadlock.c` are not compiled or measured.

## Coverage expectations

This suite has not been run against your local Linux environment in this package-generation step, so no 90% or 100% result is claimed. Coverage depends on the exact source version, compiler and runtime. The first run will show real line and branch coverage; use uncovered-line reports to add tests or adjust test seams. A 100% line-coverage number does not imply 100% branch coverage or prove memory safety.

For an enforced 90% line threshold, after a successful test run:

```bash
gcovr --root . --filter '.*(main|bst|storage|auth|audit_log|station_validation_threads)\.c$' --print-summary --fail-under-line 90
```

## Scope limitations

The package covers station AVL operations, station CSV storage, authentication/permissions, audit logging, threaded station validation, and main-program interactive integration. Connection-management files were not present in the uploaded source set and are therefore not included. If your project has `connection.c` / `connection.h` and connection-specific tests, those should be added separately.

Some OS/resource failure paths (for example `malloc()` or `pthread_create()` failures) cannot be triggered reliably without dependency injection or test hooks. Treat the coverage report as the source of truth rather than assuming every edge case is covered.
