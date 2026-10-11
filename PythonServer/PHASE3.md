# Phase 3 — Restricted C++ generator

The Phase 3 MVP is deliberately fail-closed. It supports **only** an Actor
Blueprint with one EventGraph, no Blueprint variables, and one
`ReceiveBeginPlay` event. That event may be followed by a linear chain of
`UKismetSystemLibrary::PrintString` calls using literal `InString` values.
Disconnected, branched, pure, cyclic, latent and otherwise unsupported nodes
raise errors rather than being omitted.

## CLI/API

Run the analyzer API with:

```bash
cd PythonServer
python -m pip install -e ".[api,test]"
uvicorn app.main:app --host 127.0.0.1 --port 8000
python -m pytest -q
```

`POST /generate` takes `{"snapshot": <Phase 1 JSON>, "class_name": "AMyActor"}`
and returns two in-memory files (`.h` and `.cpp`). It does **not** write to
the project or compile code.

The Phase 1 extractor is extended to include `UK2Node_Event`'s
`EventReference.GetMemberName()` as `function_name`. Without it, the
generator rejects the event as ambiguous.

## Safety and next steps

- This is a minimal deterministic generator, **not a general BP-to-C++ converter**.
- API calls use no LLM and have no access to secrets or project source files.
- PrintString optional pins are rejected when explicitly configured; default
  behavior is not guaranteed equivalent across all Blueprint versions.
- Generated classes omit project-specific `MODULE_API` exports and need
  integration into a proper UE module.
- Before expanding supported nodes, implement semantic pin resolution,
  function signature validation, control-flow lowering, and UBT validation.
- UE 5.7 compilation and generated-code builds have not been performed.
