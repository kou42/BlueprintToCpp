# Phase 2 — Python Blueprint Snapshot Analyzer

Consumes JSON exported by the UE Editor plugin in Phase 1. Requires Python 3.10+.

## Install and test

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```

## Analyze from the command line

```bash
python -m app.cli ../Saved/BlueprintSnapshots/example.json -o analyzed.json
```

## HTTP API

```bash
uvicorn app.main:app --host 127.0.0.1 --port 8000
```

`POST /analyze` accepts `{"snapshot": <Phase 1 JSON object>}`.
`GET /health` reports service health.

## Behavior and limitations

- Validates schema version, graph-local node/pin IDs, link targets, direction,
  reciprocal connections and basic pin categories.
- Preserves **parallel** Exec/Data edges with pin IDs; cycles are allowed.
- Emits a graph-only IR with source node identities for future conversion.
- Entry nodes are *heuristic*: nodes with outgoing Exec edges and no incoming
  Exec edges. A standalone event without outgoing links will not be listed.
- Does not yet resolve UE wildcard types, object/class inheritance, real/float
  subcategories, or container types. Category checks are intentionally basic.
- Does not perform Blueprint compiler-equivalent semantics, structured control
  flow, pure-node evaluation, latent actions, macros, or C++ generation.
- HTTP endpoint is intended for local development. No auth/rate limits; do not
  expose it to untrusted networks.
