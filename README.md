# CxxRelay

[![CxxRelay CI](https://github.com/MichaelMiller-/CxxRelay/actions/workflows/ci.yml/badge.svg)](https://github.com/MichaelMiller-/CxxRelay/actions/workflows/ci.yml)

**Bringing AI to existing C++ software.**

CxxRelay is a modular C++20 framework that **connects existing C++ applications to artificial intelligence**—without requiring you to rewrite their business logic.

The library uses reflection to expose selected classes, data structures, and functions, describes their interfaces in a machine-readable form, and enables AI systems to invoke them **in a controlled manner, subject to the application's permissions**.

Whether you maintain an established desktop application, engineering software, or a long-lived legacy codebase, **CxxRelay bridges proven C++ functionality and modern AI assistants**.

> **Mission:** Make existing software AI-ready instead of rebuilding it for AI.

## Documentation

See the full [documentation](https://michaelmiller-.github.io/CxxRelay) for examples, operational details and other information.

## Why CxxRelay?

Many C++ applications contain years of carefully developed domain logic. Making that functionality available through an AI chat often requires interfaces that are both discoverable and machine-readable.

CxxRelay addresses this challenge with reusable building blocks:

1. **Describe:** Explicitly register C++ types, fields, and methods.
2. **Translate:** Map values and method signatures to JSON and JSON Schema.
3. **Expose:** Publish only selected methods as AI tools.
4. **Execute:** Validate arguments and invoke the original C++ functions.
5. **Interact:** Coordinate tool calls, results, and confirmations in an AI-powered chat.

The host application remains in control of its objects, business rules, and permissions.

## Features

| Area | Capabilities |
| --- | --- |
| **Reflection** | Register C++ types, data fields, and methods; inspect metadata and method signatures |
| **Value system** | Typed, owning values, including nested arrays and objects |
| **Dynamic invocation** | Invoke registered methods with positional or named arguments |
| **Serialization** | JSON parsing, JSON output, and JSON Schema generation |
| **AI tool dispatcher** | Explicit tool exposure, argument validation, and access control |
| **Chat orchestration** | Conversation state, multiple tool calls, streaming, and cancellation |
| **Write protection** | User confirmation for mutating operations and renewed permission checks |
| **LLM integration** | Provider-independent interface and an OpenAI Responses API adapter |
| **Local prototype** | Browser chat, Python bridge, and persistent C++ console worker |

The reflection core is independent of any particular GUI framework or language model. The optional OpenAI HTTPS transport uses **libcurl**; the local mock mode does not require AI credentials.

## Architecture

```text
                  Chat GUI / CLI / other clients
                               |
                               v
                        ChatOrchestrator
                        /              \
                       v                v
                 ILLMProvider      ToolDispatcher
                       |                |
                 OpenAI / Mock    Permissions & validation
                                        |
                                        v
                             TypeRegistry / Invoker
                                        |
                                        v
                             Existing C++ application
```

**Core principle:** Reflection does not automatically grant access. A registered method becomes available to an AI model only when the host application explicitly exposes it as a tool.

## Example: Connecting an existing C++ class

You can register an existing class without modifying its implementation:

```cpp
#include <cxxrelay/core.h>
#include <cxxrelay/tool_dispatcher.h>

#include <memory>
#include <string>

using namespace CxxRelay;

class DocumentService {
public:
    std::string search(const std::string& query) const {
        return "Found: " + query;
    }
};

int main() {
    TypeRegistry registry;

    registry.type<DocumentService>("DocumentService")
        .method("search", &DocumentService::search, {
            .parameter_names = {"query"},
            .description = "Search existing documents"
        });

    auto service = std::make_shared<DocumentService>();

    ToolDispatcher tools(registry);
    tools.expose("documents_search", service, "search");

    // An authorized host can now invoke this explicitly
    // exposed C++ method through the tool dispatcher.
}
```

The registered metadata provides the foundation for JSON arguments, tool definitions, and communication with language models. **The model does not invoke C++ methods directly:** the host validates and executes the proposed operations.


## Quick start: Run the chat prototype with CMake

**The recommended development workflow is to configure CxxRelay once in CMake (or in your IDE) and start the desired prototype using a CMake target.** You do not need to launch the worker separately.

### Requirements

- **All platforms:** CMake 3.20+, a C++20 compiler, and the CxxRelay source tree.
- **Browser interface (Linux, macOS, Windows):** Python 3. The HTTP server uses the Python standard library; no additional Python packages are needed.
- **Native Windows Forms interface (Windows only):** Windows PowerShell 5.1. 
- **OpenAI mode (optional):** A compatible OpenAI model, and an API key.

### Configure the prototype in your IDE

The following CMake cache variables control **both** prototype targets:

| CMake variable                      | Default | Description                                                                                                               |
|-------------------------------------|---------|---------------------------------------------------------------------------------------------------------------------------|
| `CXXRELAY_PROTOTYPE_MODE`           | `mock`  | Select `mock` or `openai`                                                                                                 |
| `CXXRELAY_PROTOTYPE_OPENAI_API_KEY` | Empty   | Optional OpenAI API key. Prefer the `OPENAI_API_KEY` environment variable instead of placing a secret in the CMake cache. |
| `CXXRELAY_PROTOTYPE_OPENAI_MODEL`   | Empty   | OpenAI model identifier; required for `openai` mode.                                                                      |

`mock` is the default and needs no account, API key, or network connection. In `openai` mode, CMake checks the required model, an API key supplied through the cache **or** `OPENAI_API_KEY`. Invalid modes or missing requirements cause configuration to fail.

> **API key security:** CMake cache entries are stored in `CMakeCache.txt` and may also appear in generated build commands or logs. For real credentials, set `OPENAI_API_KEY` in the environment used by **both the IDE's CMake configuration and the launched target**. Do not commit local cache files or secrets. The cache variable is provided for convenience, not secure secret storage.

### Start in mock mode (recommended first run)

Configure once from your IDE, or use the command line:

```bash
cmake -S . -B build -DBUILD_TESTING=OFF -DCXXRELAY_PROTOTYPE_MODE=mock
```

On **Linux, macOS, or Windows**, start the browser chat:

```bash
cmake --build build --target prototype-python
```

Open `http://127.0.0.1:8765/` in your browser. Stop the server with `Ctrl+C` in the running terminal.

On **Windows**, you can instead start the native Windows Forms chat:

```powershell
cmake --build build --config Debug --target prototype-powershell
```

For multi-configuration generators such as Visual Studio, use the matching `--config` value (`Debug` or `Release`). Both targets are interactive; they are not run during a normal `cmake --build` or by CTest. You can also select them as build targets in a CMake-aware IDE.

### Enable OpenAI mode

First make `OPENAI_API_KEY` available to the CMake configuration process and to the prototype's runtime. On Linux/macOS, for example:

```bash
export OPENAI_API_KEY='your-api-key'
cmake -S . -B build -DBUILD_TESTING=OFF \
    -DCXXRELAY_PROTOTYPE_MODE=openai \
    -DCXXRELAY_PROTOTYPE_OPENAI_MODEL='your-model' \
    -DREFLECTION_BUILD_OPENAI_CURL=ON
cmake --build build --target prototype-python
```

On Windows PowerShell, the equivalent is:

```powershell
$env:OPENAI_API_KEY = 'your-api-key'
cmake -S . -B build -DBUILD_TESTING=OFF `
    -DCXXRELAY_PROTOTYPE_MODE=openai `
    -DCXXRELAY_PROTOTYPE_OPENAI_MODEL=your-model `
    -DREFLECTION_BUILD_OPENAI_CURL=ON
cmake --build build --config Debug --target prototype-powershell
```

The exact model name must be one available to your API account. In an IDE, set the non-secret variables in its **CMake settings** and supply `OPENAI_API_KEY` through a trusted environment configuration. After changing the CMake settings, reconfigure and launch the same prototype target again; you do not need to change the scripts.

### Fallback: Run the scripts directly

Direct script execution remains available for development or troubleshooting, but **CMake targets are the preferred workflow**.

On **Linux/macOS**, the shell script configures and builds the worker before starting the browser interface:

```bash
<BUILD>/src/prototype/run.sh mock

# Optional OpenAI mode
export OPENAI_API_KEY='your-api-key'
export OPENAI_MODEL='your-model'
<BUILD>/src/prototype/run.sh openai
```

On **Windows**, build and launch the Windows Forms prototype using the PowerShell scripts. Run the UI with Windows PowerShell in STA mode:

```powershell
powershell.exe -NoProfile -STA -File .\prototype\start.ps1 -Mode mock

# Optional OpenAI mode
$env:OPENAI_API_KEY = 'your-api-key'
$env:OPENAI_MODEL = 'your-model'
powershell.exe -NoProfile -STA -File .\prototype\start.ps1 -Mode openai
```

The direct scripts use the worker's normal `OPENAI_API_KEY` and `OPENAI_MODEL` environment variables. Unlike the CMake-target workflow, they are responsible for their own build configuration and worker path selection. 

The mock provider recognizes a limited set of prompts. Try `List all documents`, `Search for Bericht`, or `Rename document 1 to Abschlussbericht` in the mock UI. A rename is performed only after explicit confirmation. 

The OpenAI adapter uses `store: true` and `previous_response_id` for conversation continuation. Relevant prompts and tool data may therefore be sent to the provider.



## Build and test

```bash
# Build the library and examples (without the Catch2 test dependency)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build build --parallel

# Run tests with Catch2 v3 installed
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Security principles

CxxRelay treats output from AI models as **untrusted input**.

- **Allowlisting:** Only explicitly exposed tools can be invoked.
- **Permissions:** The host supplies the user's authorization context; the model cannot grant permissions.
- **Validation:** JSON arguments and C++ type conversions are validated.
- **Confirmation:** Mutating operations require approval through a trusted host interface.
- **Limits:** The chat orchestrator limits model/tool call chains and supports cooperative cancellation.

User confirmation does not replace domain-specific validation or transactions. The current local web prototype is **a development tool**, not a production-ready multi-user API.

## Project status and roadmap

**Status: Functional development prototype.** Reflection, JSON support, the tool dispatcher, chat orchestration, and a local browser/worker demonstrator are implemented. The OpenAI integration has been implemented but has not yet been verified end-to-end with live API credentials.

Planned next steps include:

- Integration into an existing desktop GUI with asynchronous processing.
- Stabilization and versioning of the library's public interfaces.
- Optional **C ABI / FFI interfaces** for other languages and processes.
- An optional **MCP adapter** for standardized external tool clients.
- Expanded testing, documentation, and production-grade session and security concepts.

**The C API and MCP adapter are on the roadmap; they are not currently provided as finished interfaces.**

## Philosophy

**CxxRelay does not replace existing C++ software. It makes that software's capabilities accessible to AI.**

*Bringing AI to existing C++ software.*
