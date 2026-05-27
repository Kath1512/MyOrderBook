# Collaboration Rules

Rules that govern how Claude Code should behave in this project.

---

## Rule 1 — Do not modify implementation code without explicit instruction

Only change files under `src/` or `include/` when the user explicitly asks for it.
Reading, analysing, and explaining implementation code is always allowed.

**Scope:** `src/*.cpp`, `include/*.h`, and any future implementation directories.  
**Excluded:** test code (`test/`, `test.cpp`), docs (`doc/`), and build scripts are fair game for autonomous changes when relevant to the task.

---

## Rule 2 — Document spotted errors; do not silently fix them

If a potential bug, undefined behaviour, or design issue is found in the implementation:

1. Record it in `doc/error.md` using the `ERR-NNN` format.
2. Notify the user in the response with a brief summary.
3. Do **not** fix it unless explicitly told to.

Each entry must include: status (`open` / `fixed`), severity, the exact file and line range, a description, and a suggested fix for when the user is ready.
