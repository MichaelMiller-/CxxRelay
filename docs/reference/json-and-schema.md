---
title: JSON & Schema
---

# JSON & Schema

`Value` is the intermediate form between C++ objects, JSON data, and AI tools.

## Serialization

```cpp
#include <cxxrelay/json.h>

using namespace CxxRelay;

Value request = parse_json(R"({"query":"report"})");
std::string encoded = to_json(request);
```

The parser and writer handle nested arrays and objects and preserve supported 64-bit integer values within the C++ process.

## Method argument schemas

Registered parameter metadata can be expressed as JSON Schema (Draft 2020-12). For example:

```json
{
  "type": "object",
  "properties": {
    "query": { "type": "string" }
  },
  "required": ["query"],
  "additionalProperties": false
}
```

Schemas support structured and nested registered types via definitions and references. `std::optional<T>` currently maps to a nullable value, **not** an omitted property.

!!! note "Schema is not the only validation step"
    The dispatcher validates the supported argument shapes and the invoker performs the final C++ conversion. CxxRelay does not currently implement a general-purpose validator for every JSON Schema keyword.

!!! warning "Large integers in JavaScript"
    Browser `Number` values cannot represent every 64-bit integer exactly. For large identifiers, use an explicitly designed string-based contract where required.
