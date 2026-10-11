# Phase 4 — Typed Branch MVP

Extends the Phase 3 restricted generator with a **single** `K2Node_IfThenElse`
following `ReceiveBeginPlay`, with a **literal unlinked bool Condition**
and up to one `PrintString` leaf per True/False arm.

The new `typed_ir.py` retains a typed BoolLiteral and branch source pin
identity. `generator_dispatch.py` selects a strict generator based on the
presence of a Branch node. It does not silently fall back when conversion
fails. The API remains `POST /generate`.

## Tests

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```

## Deferred deliberately

- Connected Branch conditions, variables, numeric expressions and type coercion.
- General-purpose CFG structuring, nested branches, merge points and loops.
- Arbitrary function calls and signature resolution.
- UE-specific default pin semantics and complete PrintString optional inputs.
- UBT compile, runtime behavior equivalence and AI-assisted code generation.

No UE 5.7 engine compilation or Python test run has been performed here.
