---
title: Developer Guide
---

# Developer Guide

Build a small, working integration before changing your existing GUI.

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **Getting started**

    ---

    Prerequisites, build, and first run.

    [Start here](getting-started.md)

-   :material-code-json: **Register C++ interfaces**

    ---

    Describe structures and methods without rewriting their implementations.

    [See registration](registration.md)

-   :material-shield-check-outline: **Expose AI tools**

    ---

    Publish methods, assign permissions, and inspect the invocation flow.

    [See tool calling](tool-calling.md)

-   :material-message-text-outline: **Run the chat**

    ---

    Launch the browser or native Windows Forms prototype using CMake.

    [Open the prototype guide](chat-prototypes.md)

-   :material-test-tube: **Build & test**

    ---

    Catch2, CTest, local builds, and troubleshooting.

    [Run the tests](build-and-test.md)

</div>

!!! tip "Recommended workflow"
    Configure CMake in your IDE, select the desired `prototype-*` target, and run in **mock** mode first. The C++ worker is built automatically.
