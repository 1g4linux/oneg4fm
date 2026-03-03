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
- `routing.defaultBackend = Auto` for native-only requests
- `routing.defaultBackend = Gio` when any endpoint is URI-shaped
- `routing.destinationBackend = Auto`

Before execution, the adapter runs `FileOpsContract::preflight()` so unsupported
backend/capability combinations fail early with a clear core-generated reason.

### Signal/Conflict Bridge

- Progress: core `ProgressSnapshot` -> Qt `progress(FileOpProgress)`.
  Delivery is level-triggered/coalesced in the adapter (single pending snapshot,
  latest value wins) with a max ~20 Hz emission target plus a meaningful-byte
  delta gate; pending progress is force-flushed before `finished(...)`.
  `phase` is forwarded so UI can distinguish `Running` from `Finalizing`.
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
- `libfm-qt/src/core/trashjob.cpp`
- `libfm-qt/src/core/untrashjob.cpp`
- `libfm-qt/src/core/fileops_bridge_policy.cpp`
- `libfm-qt/src/core/fileops_request_assembly.cpp`

### Routing Policy (`classifyPathForFileOps`)

`RoutingClass` values:

- `RoutingClass::CoreLocal`: native local path with a valid local path string.
- `RoutingClass::LegacyGio`: URI/non-native path that must run through core
  with backend `Gio`.
- `RoutingClass::Unsupported`: invalid/empty path input.

Rules:

- Copy/move/delete requests route through `FileOpsContract::run` for both
  `RoutingClass::CoreLocal` and `RoutingClass::LegacyGio`.
- `RoutingClass::Unsupported` is a hard error; no unsafe local fallback.
- Core-routed jobs enforce adapter-side input bounds before assembly:
  `kMaxPathsPerRequest` (currently 4096).
- When `LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT=0`, copy/move/delete/trash/untrash
  fail fast with `G_IO_ERROR_NOT_SUPPORTED`; adapter jobs do not execute legacy
  retry-capable mutation paths.
- `classifyPathForFileOps` is deterministic and does not probe filesystem
  metadata to choose behavior.
- Adapters do not call GIO file-op mutation APIs directly for these operations.

### `FileTransferJob` Bridge

- For copy/move (not link), requests route through core contract for both local
  and legacy-GIO classifications.
- Link mode remains on legacy link helpers; only copy/move is in scope for this
  contract adapter path.
- Adapter builds `FileOpsContract::TransferRequest` and routes via typed
  request conversion + event-stream bridge:
  `run(toRequest(request), handlers, streamHandlers)`.
- Request assembly is centralized in `FileOpsRequestAssembly::buildTransferRequest`
  with deterministic 1:1 mapping from bridge inputs to contract fields.
- Request identity is explicit: every assembled request carries a generated
  `common.opId` and source snapshot metadata in
  `common.sourceSnapshots` / `common.uiContext.initiator`.
- Uses `DestinationMappingMode::ExplicitPerSource` for per-item destination.
- Conflict dialog integration:
  - `PromptEvent` triggers legacy UI prompt via `askRename(...)`
  - `ConflictEvent` consumes that response and maps
    overwrite/skip/rename (+ `*All`) directly to core conflict resolutions
- Progress from core is translated into legacy finished/current progress values.
- Completion/cancel mapping is `OpResult`-based (`OpStatus::{Success,Cancelled,Failed}`
  plus `OpCounters`) rather than adapter-local result heuristics.
- Non-conflict errors are surfaced once through legacy error signals; adapter
  does not run retry loops.
- Error messages preserve structured core diagnostics by appending
  `engine_code`, `step`, and `errno` from `OpResult::diagnostics`.

### `DeleteJob` Bridge

- Delete routes through core contract for both `CoreLocal` and `LegacyGio`
  classifications.
- Adapter builds `FileOpsContract::DeleteRequest` via
  `FileOpsRequestAssembly::buildDeleteRequest`.
- Progress and totals are aggregated from core snapshots into legacy
  `finishedAmount`/`totalAmount`, and completion uses `runOp(...)` status/counters.
- Adapter does not pre-scan with `TotalSizeJob`.
- Unsupported routing class is reported as a critical error and operation abort.

### `TrashJob` and `UntrashJob` Bridge

- Trash/untrash requests are translated into core contract operations
  (`Operation::Trash`, `Operation::Untrash`) with backend `Gio` via
  `FileOpsContract::TrashRequest` / `FileOpsContract::UntrashRequest`,
  assembled by `FileOpsRequestAssembly`.
- Trash now subscribes to core `onProgress` and maps normalized progress/totals
  through `OpResult` counters (`runOp(request, handlers)`).
- Untrash prompt/conflict UI wiring uses core event-stream callbacks
  (`PromptEvent` + `ConflictEvent`) and maps responses mechanically to
  core conflict resolutions.
- Untrash completion/cancellation maps through `OpResult` (`toOpResult(...)`
  over event-stream `run(...)` result).
- Adapter does not probe mount/filesystem metadata or run retry loops for
  core-routed trash/untrash paths.

## Adapter Invariant

Both adapters are semantic pass-through layers:

- Core decides traversal, conflict policy effects, progress monotonicity,
  cancellation semantics, and cleanup behavior.
- Adapters only transform types and bridge UI/job events.
- For `libfm-qt` file-op jobs (copy/move/delete/trash/untrash):
  - no adapter-side planner/pre-scan (`TotalSizeJob`) runs
  - no adapter retry loop is applied
  - no adapter conflict policy heuristics are added beyond mapping UI choices
  - no filesystem probing is used for routing decisions
  - cancellation bridging is centralized in request assembly and maps UI cancel
    state to `CancelHandle.cancel()` / `CancelHandle.isCancelled()`
