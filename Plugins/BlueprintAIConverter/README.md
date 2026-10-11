# BlueprintAIConverter — Phase 1 (Unreal Engine 5.7)

Editor-only plugin exporting selected Blueprint assets to versioned JSON snapshots. This phase does not call Python or an AI service and does not generate C++.

## Installation
Copy `Plugins/BlueprintAIConverter` into a UE 5.7 C++ project's `Plugins` folder, build the Editor target, and enable the plugin if needed.

## Export
In the Content Browser, right-click a Blueprint asset and choose **Export Blueprint Snapshot (JSON)**. Snapshots are saved in `<Project>/Saved/BlueprintSnapshots/`. Multiple selections are supported.

## Tests
Use Session Frontend > Automation, filter `BlueprintAIConverter.Snapshot`, or run the Editor command line with `-ExecCmds="Automation RunTests BlueprintAIConverter.Snapshot; Quit" -unattended -nop4 -log`.

## Schema v1
Includes engine version, asset path, parent class, event/function/macro graphs, node GUIDs/classes/titles/positions, pin IDs/types/defaults/connections, and Blueprint-declared variables.

## Limitations
- UE 5.7 compile and Automation Test execution have **not** been verified in an installed engine.
- Snapshot data is not yet a semantic IR; no C++ generation in Phase 1.
- No complete CDO defaults, SCS hierarchy, interface/delegate signatures, timeline curves, or widget animation export.
- Pin container terminal types and inherited properties require later phases.
- Blueprint variable property flags are serialized as strings to avoid JSON 64-bit precision loss.
- This plugin reads Blueprint assets; it does not modify them or make network requests.
