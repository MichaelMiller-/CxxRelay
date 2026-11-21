---
title: Getting Started
---

# Getting Started

## Requirements

| Component | Requirement |
| --- | --- |
| Build system | CMake **3.20+** |
| Compiler | C++20-capable compiler (GCC, Clang, or MSVC) |
| Tests | Catch2 v3 when `BUILD_TESTING=ON` |
| Browser chat | Python 3, standard library only |
| Windows native chat | Windows PowerShell 5.1 + Windows Forms |
| Live OpenAI mode | API key, available model, and libcurl transport |

## 1. Configure and build

=== "Linux / macOS"

    ```bash
    cmake -S . -B build -DBUILD_TESTING=OFF -DCXXRELAY_PROTOTYPE_MODE=mock
    cmake --build build --parallel
    ```

=== "Windows / MSVC"

    ```powershell
    cmake -S . -B build -DBUILD_TESTING=OFF -DCXXRELAY_PROTOTYPE_MODE=mock
    cmake --build build --config Debug
    ```

## 2. Launch a prototype

=== "Browser / Python"

    ```bash
    cmake --build build --target prototype-python
    ```

    Open <http://127.0.0.1:8765/>.

=== "Windows Forms"

    ```powershell
    cmake --build build --config Debug --target prototype-powershell
    ```

    A native chat window opens; Python and a local HTTP server are not needed.


## Next steps

[Register your classes](registration.md) → [expose a tool](tool-calling.md) → [try the complete chat workflow](chat-prototypes.md).
