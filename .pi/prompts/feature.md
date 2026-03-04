---
name: feature
description: Implement a new feature end-to-end
---

# Feature Request: {{description}}

Execute the full development cycle for this feature:

1. Load the **plan** skill. Decompose this request into subtasks.
2. For each subtask, load the appropriate specialist skill (terrain, engine, shader, actor) and implement the changes.
3. Build the project: `cmake --build build -j$(nproc)`. Fix any compilation errors.
4. Load the **test** skill. Write quantitative visual tests for the changes.
5. Build and run tests: `cmake --build build --target delve_tests && ./build/delve_tests`. Fix any test failures.
6. Load the **review** skill. Self-review the changes.
7. Create a PR with a structured description.
