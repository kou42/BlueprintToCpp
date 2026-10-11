# Phase 5 — Typed variable declaration and literal Set MVP

Adds preservation of `UK2Node_Variable` member names in Phase 1 snapshots,
through the parser and IR. A strict generator handles a direct Actor Blueprint
with one BeginPlay event and a linear sequence of VariableSet nodes assigning
**unlinked literals**. It emits UPROPERTY members for supported bool/int/int64
types, with restricted float support only when precision is explicitly marked.

## Deliberate constraints

- VariableGet is detected but **rejected** until data dependency resolution
  and evaluation order are implemented.
- Connected values, Blueprint defaults not representable as literals,
  containers, object types, inherited members, branching and loops are rejected.
- Blueprint variables' metadata/flags are not yet mapped to UPROPERTY specifiers;
  the generated EditAnywhere/BlueprintReadWrite declaration is a placeholder
  policy and must be reviewed before production use.
- No AI generation or UBT build verification is performed.
- Generated code may require additional integration into the target UE module.

## Tests

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```
