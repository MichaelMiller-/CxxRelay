---
title: Register C++ Interfaces
---

# Register C++ Interfaces

Existing structures and classes can be described **without modifying their source definitions**.

## Register fields

```cpp
#include <cxxrelay/core.h>

using namespace CxxRelay;

// existing user-code
struct Point3D {
    double x{}, y{}, z{};
};

TypeRegistry registry;
registry.type<Point3D>("Point3D")
    .field("x", &Point3D::x)
    .field("y", &Point3D::y)
    .field("z", &Point3D::z);

Point3D point{1.0, 2.0, 3.0};
auto data = registry.to_value(point);
Point3D restored = registry.from_value<Point3D>(data);
```

The registry can also describe nested registered structures, `std::vector<T>`, and `std::optional<T>`.

## Register methods

```cpp
// existing user-code
struct DocumentService {
    std::string search(std::string query) const {
        return "Found: " + query;
    }
};

registry.type<DocumentService>("DocumentService")
    .method("search", &DocumentService::search, {
        .parameter_names = {"query"},
        .description = "Search existing documents"
    });
```

A `MethodDescriptor` now carries the metadata needed to build schemas and perform controlled invocation.

!!! warning "Registration ≠ AI access"
    A method in `TypeRegistry` is **not** automatically available to an LLM. It must also be explicitly published through `ToolDispatcher::expose()`.

[Continue to tool calling](tool-calling.md).
