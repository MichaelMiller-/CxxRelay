---
title: Use Cases
---

# Use Cases

These examples illustrate what CxxRelay enables; they do not imply that CxxRelay ships a document database or other application-specific services.

<div class="grid cards" markdown>

-   :material-magnify: **Find information**

    ---

    *"Show the documents related to the annual report."*

    The assistant invokes an explicitly exposed read-only search method and summarizes the result.

-   :material-pencil-outline: **Modify data**

    ---

    *"Rename this document to Final Report."*

    CxxRelay checks permissions and asks the user to approve the exact proposed operation.

-   :material-cube-outline: **Explore engineering objects**

    ---

    *"Which nodes belong to this model?"*

    Registered object metadata and application-specific query methods make structured data accessible.

-   :material-puzzle-outline: **Extend legacy software**

    ---

    Add a conversational entry point while the original GUI and business logic continue to operate.

</div>

## A realistic adoption path

1. Expose one useful **read-only** method.
2. Test it with the local mock chat.
3. Add structured inputs and outputs.
4. Enable sensitive operations only with the appropriate host authorization and confirmation.
5. Integrate the workflow into the actual application GUI.

For a working starting point, see the [Developer Guide](../developer/index.md).
