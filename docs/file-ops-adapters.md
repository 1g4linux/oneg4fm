# File-Ops Adapters (Qt and `libfm-qt`)

This document describes how adapter layers consume the core file-ops contract
without changing semantics.

## Qt Adapter (`src/backends/qt/qt_fileops.cpp`)

Primary adapter class: `QtFileOps`.

### Responsibilities

- Convert `FileOpRequest` (`src/core/ifileops.h`) into
  `FileOpsContract::Request`.
- Forward progress and conflict events to Qt signals.
- Bridge cancellation and completion status.
- Avoid local planning/traversal logic; core owns semantics.

### Request Mapping

| `FileOpRequest` field | Adapter behavior |
| --- | --- |
| `type` | Maps to core `Operation` (`Copy`, `Move`, `Delete`). |
| `sources` | Required and converted to native byte paths. |
| `sources` URI/native shape | Adapter also maps per-source endpoint kind (`routing.sourceKinds`) using URI-shape detection (`"://"`). |
| `destination` | Required for copy/move; rejected for delete. |
| `destination` URI/native shape | Mapped to `routing.destinationKind` for copy/move. |
| `overwriteExisting` | Used for copy/move (`Overwrite` vs `Skip` conflict policy). Rejected for delete. |
| `promptOnConflict` | Enables core `ConflictPolicy::Prompt`; rejected for delete and rejected when combined with `overwriteExisting=true`. |
| `followSymlinks` | `true` is rejected; adapter enforces no-follow path. |
| `preserveOwnership` | Forwarded to core metadata policy. |

Adapter-fixed core settings:

- `symlinkPolicy.followSymlinks = false`
- `symlinkPolicy.copyMode = CopyLinkAsLink`
- `atomicity.requireAtomicReplace = false`
- `atomicity.bestEffortAtomicMove = true`
- `linuxSafety.requireOpenat2Resolve = true`
- `linuxSafety.requireLandlock = false`
- `linuxSafety.workerMode = InProcess`
- `routing.defaultBackend = Auto`
- `routing.destinationBackend = Auto`

Before execution, the adapter runs `FileOpsContract::preflight()` so unsupported
backend/capability combinations (for example URI paths requiring unavailable GIO
backend integration) fail early with a clear core-generated reason.

### Signal/Conflict Bridge

- Progress: core `ProgressSnapshot` -> Qt `progress(FileOpProgress)`.
- Conflict: core `ConflictEvent` -> Qt `conflictRequested(FileOpConflict)`.
- Prompt conflict requires a connected responder; otherwise request fails.
- Cancellation:
  - `cancel()` sets shared cancel flag
  - if waiting for conflict response, cancellation injects `Abort`
- Completion:
  - success -> `finished(true, "")`
  - cancel -> `finished(false, "Operation cancelled")`
  - failure -> core message fallback

## `libfm-qt` Compatibility Bridge

Relevant files:

- `libfm-qt/src/core/filetransferjob.cpp`
- `libfm-qt/src/core/deletejob.cpp`
- `libfm-qt/src/core/fileops_bridge_policy.cpp`

### Routing Policy (`classifyPathForFileOps`)

`RoutingClass` values:

- `RoutingClass::CoreLocal`: native local path on non-remote filesystem.
- `RoutingClass::LegacyGio`: URI/non-native/remote path handled by legacy GIO.
- `RoutingClass::Unsupported`: native-looking path that cannot be classified safely.

Rules:

- Core contract is used only for `RoutingClass::CoreLocal`.
- `RoutingClass::Unsupported` is a hard error; no unsafe local fallback.
- Legacy GIO path remains for `trash:///`, remote mounts, and other URI schemes.

### `FileTransferJob` Bridge

- For copy/move (not link), core-local source+destination route through
  `FileOpsContract::run`.
- Uses `DestinationMappingMode::ExplicitPerSource` for per-item destination.
- Conflict dialog integration:
  - Legacy UI asks user via `askRename(...)`
  - overwrite/skip map directly to core conflict resolutions
  - rename is handled by providing a new destination and retrying with a new
    core request
- Progress from core is translated into legacy finished/current progress values.
- Cancellation and retry/error actions remain driven by legacy job mechanics.

### `DeleteJob` Bridge

- Core-local delete routes through core contract delete.
- Progress is aggregated into legacy `finishedAmount`.
- Unsupported routing class is reported as a critical error and operation abort.

## Adapter Invariant

Both adapters are semantic pass-through layers:

- Core decides traversal, conflict policy effects, progress monotonicity,
  cancellation semantics, and cleanup behavior.
- Adapters only transform types and bridge UI/job events.
