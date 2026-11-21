---
title: Expose AI Tools
---

# Expose AI Tools

`ToolDispatcher` forms the authorization boundary between reflection metadata and an AI-requested function call.

## Publish a method

```cpp
#include <cxxrelay/core.h>
#include <memory>
#include <string>

using namespace CxxRelay;

struct DocumentService {
    std::string search(std::string query) const {
        return "Found: " + query;
    }
};

TypeRegistry registry;
registry.type<DocumentService>("DocumentService")
    .method("search", &DocumentService::search, {
        .parameter_names = {"query"},
        .description = "Search documents"
    });

auto service = std::make_shared<DocumentService>();
ToolDispatcher tools(registry);
tools.expose("documents_search", service, "search");

ToolContext user{.principal = "demo", .allow_read = true};
ToolResult result = tools.dispatch(
    "documents_search", R"({"query":"Report"})", user);
```

The dispatcher validates JSON arguments, checks permissions, invokes the selected C++ method, and returns a structured result.

## Read and write operations

| Operation | Expected treatment |
| --- | --- |
| Read-only | May execute within the user's read permissions |
| Write | Requires write permission **and** host-side confirmation |
| Unknown tool | Rejected; registration alone is not enough |
| Invalid arguments | Rejected before method invocation |

!!! danger "Never trust model-supplied authorization"
    `ToolContext` must be created by trusted application code. The model cannot grant itself permissions or approve a write operation.

For the end-to-end approval and continuation flow, see [Chat and tool execution](../reference/chat-and-ipc.md).
