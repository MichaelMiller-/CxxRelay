---
title: Technical Reference
---

# Technical Reference

A compact explanation of CxxRelay's internal contracts and boundaries. This is **not** the planned full formal specification; that can later be maintained in LaTeX.

<div class="grid cards" markdown>

-   :material-sitemap-outline: **Architecture**

    ---

    How the layers fit together and who owns each responsibility.

    [Architecture](architecture.md)

-   :material-shape-outline: **Reflection & Value**

    ---

    Runtime metadata, type conversion, and invocation semantics.

    [Data model](reflection-and-value.md)

-   :material-code-json: **JSON & Schema**

    ---

    How values and signatures become transportable data.

    [Serialization](json-and-schema.md)

-   :material-shield-lock-outline: **Tool security**

    ---

    Tool publication, permission checks, and write approval.

    [Security model](tool-security.md)

-   :material-chat-processing-outline: **Chat & IPC**

    ---

    Provider calls, tool results, and the persistent worker protocol.

    [Workflow](chat-and-ipc.md)

-   :material-map-marker-path: **Status & roadmap**

    ---

    Current implementation status and explicit future work.

    [Project roadmap](status-and-roadmap.md)

</div>
