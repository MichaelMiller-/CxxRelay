---
title: Build & Test
---

# Build & Test

## Build without tests

```bash
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel
```

For Visual Studio or other multi-config generators, add `--config Debug` or `--config Release` to the build command.

## Run Catch2 through CTest

```bash
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

[Catch2](https://github.com/catchorg/Catch2) is a test framework for unit-tests. The buildsystem automatically download and build the framwork using [CPM](https://github.com/cpm-cmake/CPM.cmake).

## CI strategy

| Platform | Compiler | Main checks |
| --- | --- | --- |
| Linux | GCC | C++20 build, Catch2/CTest |
| Linux | Clang | C++20 build, Catch2/CTest |
| Windows | MSVC | C++20 build, Catch2/CTest |

The CI workflow is separate from the documentation workflow. Interactive `prototype-*` targets must not be part of the normal CI build or CTest.

## Common issues

| Symptom | Check |
| --- | --- |
| Python target fails | Python 3 must be installed and discoverable at runtime. |
| Windows UI does not launch | Use Windows PowerShell 5.1 and STA mode; check the worker build. |
| OpenAI mode rejected at configure time | Model, API key, and libcurl transport must be available. |
| Tool invocation rejected | Confirm the method is exposed and the host grants permission. |


!!! info "Validation scope"
    The project contains C++ unit tests and a small mock-driven worker E2E suite. Passing these tests does not imply a live OpenAI integration or production deployment has been validated.
