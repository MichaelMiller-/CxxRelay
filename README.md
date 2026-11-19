# CxxRelay

**Bringing AI to existing C++ software.**

CxxRelay is a modular C++20 framework that **connects existing C++ applications to artificial intelligence**—without requiring you to rewrite their business logic.

The library uses reflection to expose selected classes, data structures, and functions, describes their interfaces in a machine-readable form, and enables AI systems to invoke them **in a controlled manner, subject to the application's permissions**.

Whether you maintain an established desktop application, engineering software, or a long-lived legacy codebase, **CxxRelay bridges proven C++ functionality and modern AI assistants**.

> **Mission:** Make existing software AI-ready instead of rebuilding it for AI.

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

> **Note on the current codebase:** The project is now called **CxxRelay**, but its headers, C++ namespace, and CMake targets still use `reflection` and `CxxRelay::core`. The underlying code has not yet been renamed.

## Quick start: End-to-end prototype

The included prototype connects a script-based browser chat to a persistent C++ worker through a local Python HTTP server.

**Requirements:** Linux, a C++20 compiler, CMake 3.20 or later, and Python 3. The offline prototype does not require Qt, Node.js, or additional Python packages.

```bash
./prototype/run.sh mock
```

Then open this address in your browser:

```text
http://127.0.0.1:8765/
```

Try these example requests:

- `List all documents`
- `Search for Bericht`
- `Rename document 1 to Abschlussbericht`

A rename operation is executed only after you confirm it in the chat UI. The mock provider is deterministic and is intended solely for workflow testing. Its supported prompts may differ from the English examples above; see the prototype documentation for the exact commands.

### Optional: Connect to OpenAI

With the libcurl development package installed and valid API credentials:

```bash
export OPENAI_API_KEY='...'
export OPENAI_MODEL='...'
./prototype/run.sh openai
```

Do **not** put API keys into source code, the browser UI, or your Git repository. The current OpenAI adapter continues conversations using `store: true` and `previous_response_id`, so chat content and potentially tool data are sent to the provider.

For details, see [`prototype/README.md`](prototype/README.md).

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

The HTTPS transport is optional and can be disabled with `-DREFLECTION_BUILD_OPENAI_CURL=OFF` if needed.

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
