---
title: CxxRelay Documentation
hide:
  - toc
---

# CxxRelay

<span class="cxxrelay-tagline">Bringing AI to existing C++ software.</span>

CxxRelay connects established C++ applications to modern AI assistants **without replacing their business logic**. Register selected C++ capabilities, expose only approved operations, and execute AI-requested actions under host-controlled permissions.

<div class="grid cards" markdown>

-   :material-compass-outline: **Vision & User Story**

    ---

    Understand the motivation, user experience, and real-world scenarios.

    [Explore the vision](vision/index.md)

-   :material-code-braces: **Developer Guide**

    ---

    Build the project, register C++ methods, and run a local chat prototype.

    [Get started](developer/index.md)

-   :material-layers-outline: **Technical Reference**

    ---

    Explore the architectural boundaries, data model, security, and known limits.

    [View the reference](reference/index.md)

</div>

## One framework, three responsibilities

```mermaid
flowchart LR
    A[Existing C++ application] --> B[Reflection & invocation]
    B --> C[Authorized AI tools]
    C <--> D[Chat orchestrator]
    D <--> E[LLM provider]
    U[User interface] <--> D
```

!!! info "Current project stage"
    CxxRelay is a **working development prototype**. Reflection, JSON, tool dispatching, chat orchestration, and local test UIs exist. The C API, MCP adapter, and reusable production GUI integration are planned.

**Suggested starting point:** [Read the user story](vision/user-story.md), then [run the mock prototype](developer/chat-prototypes.md).
