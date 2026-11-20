---
title: Status & Roadmap
---

# Status & Roadmap

## Implemented in the current prototype

| Capability | Status |
| --- | --- |
| Value representation and type/field reflection | **Implemented** |
| Method descriptors and dynamic invocation | **Implemented** |
| JSON conversion and schema generation | **Implemented** |
| Allowlisted tool dispatch with permissions | **Implemented** |
| Chat orchestration, continuation, approvals | **Implemented** |
| Provider-independent API and OpenAI adapter | **Implemented; live API not verified here** |
| Local browser UI and native Windows Forms UI | **Development prototypes** |
| Cross-platform CMake and CI workflow | **Development workflow available** |

## Planned

- A reusable, non-blocking GUI adapter for the host application's UI framework.
- A stable and documented C ABI for language-neutral interop.
- An optional MCP adapter.
- Stronger session isolation, host transactions, durable audit, and deployment controls.
- A formal technical specification in LaTeX.

!!! note "Naming transition"
    The public project name is **CxxRelay**. Existing source artifacts still contain `reflection` names; documentation examples intentionally match the currently available code.

## Documentation strategy

| Layer | Purpose | Intended depth |
| --- | --- | --- |
| **Vision & User Story** | Explain motivation and benefits | Concise and accessible |
| **Developer Guide** | Help developers get started and integrate | Practical examples |
| **Technical Reference** | Explain interfaces, constraints, and architecture | Focused summary; future formal LaTeX treatment |
