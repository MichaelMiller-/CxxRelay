---
title: Vision
---

# Vision & User Story

**Preserve proven software. Make its capabilities accessible to AI.**

Long-lived C++ applications often embody specialized knowledge and mature business rules. Rewriting them to add a conversational interface is expensive and risky. CxxRelay provides an integration layer between those applications and AI systems.

<div class="grid cards" markdown>

-   :material-account-voice: **User story**

    ---

    Who benefits, what they need, and what success means.

    [Read the story](user-story.md)

-   :material-lightbulb-outline: **Use cases**

    ---

    Search, inspect, and update application data with controlled actions.

    [Explore scenarios](use-cases.md)

</div>

## Guiding principles

| Principle | Meaning |
| --- | --- |
| **Reuse** | Keep established algorithms and domain rules in C++. |
| **Explicit exposure** | Reflection does not automatically make functions callable by AI. |
| **Human control** | Permissions and write approvals belong to the application. |
| **Provider independence** | Application interfaces are not tied to a single LLM. |
| **Incremental adoption** | Start with a small set of read-only functions and expand safely. |

!!! quote "The idea"
    The AI can suggest an action. Only the host application decides whether and how that action is executed.
