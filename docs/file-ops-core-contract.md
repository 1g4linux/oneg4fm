# Core File-Ops Contract

This document defines the behavior of `PCManFM::FileOpsContract` in
`src/core/file_ops_contract.h` and `src/core/file_ops_contract.cpp`.

## Entry Point

- API: `PCManFM::FileOpsContract::run(const Request&, const EventHandlers&)`
- Supported operations today: `Copy`, `Move`, `Delete`
- Rejected operations today: `Mkdir`, `Link` (`EngineErrorCode::UnsupportedOperation`)

## `Request` Fields

The contract accepts a `Request` and validates it before planning/execution.

| Field | Behavior |
| --- | --- |
| `operation` | Must be `Copy`, `Move`, or `Delete`. |
| `sources` | Must be non-empty; each source must be non-empty and stat-able during plan build. |
| `destination.targetDir` | Required for `Copy`/`Move`; ignored for `Delete`. |
| `destination.mappingMode` | `SourceBasename` or `ExplicitPerSource`. |
| `destination.explicitTargets` | Required count match when `ExplicitPerSource` is used. |
| `conflictPolicy` | `Overwrite`, `Rename`, `Skip`, or `Prompt`. |
| `symlinkPolicy.followSymlinks` | Must be `false`; `true` is rejected. |
| `symlinkPolicy.copyMode` | Must be `CopyLinkAsLink`; `CopyTarget` is rejected. |
| `metadata.preserveOwnership` | Honored and forwarded to core fs ops. |
| `metadata.preservePermissions` | Must be `true`; disabling is rejected. |
| `metadata.preserveTimestamps` | Must be `true`; disabling is rejected. |
| `atomicity.requireAtomicReplace` | Must be `false`; `true` is currently rejected. |
| `atomicity.bestEffortAtomicMove` | Must be `true` for move; disabling is rejected. |
| `cancelGranularity` | Accepted by the contract; cancellation remains cooperative through callback checkpoints. |
| `cancellationRequested` | Polled during validation/planning/execution and conflict handling. |
| `linuxSafety.requireOpenat2Resolve` | If `true`, validation probes hardened `openat2` resolution support. |
| `linuxSafety.requireLandlock` | Requires `workerMode = SandboxedThread`; otherwise request is rejected. |
| `linuxSafety.requireSeccomp` | Requires `workerMode = SandboxedThread`; otherwise request is rejected. |
| `linuxSafety.workerMode` | `InProcess` or `SandboxedThread`. |

The `linuxSafety.*` fields are the members of `LinuxSafetyRequirements`.

## Conflict and Event Model

### `EventHandlers::onProgress`

- Receives `ProgressSnapshot`.
- Totals (`filesTotal`, `bytesTotal`) are planned first and then kept stable.
- Done counters are monotonic and clamped to totals.
- `currentPath` remains stable (non-empty once set).

### `EventHandlers::onConflict`

- Used when `conflictPolicy = Prompt`.
- Receives `ConflictEvent` with:
  - `kind` (`DestinationExists`)
  - `sourcePath`
  - `destinationPath`
  - `destinationIsDirectory`
- Must return a `ConflictResolution`.
- `OverwriteAll`, `SkipAll`, `RenameAll` are normalized and remembered for
  subsequent conflicts.
- Rename policy uses deterministic suffixes (`" (copy)"`, then `" (copy N)"`).

If prompt mode is requested without `onConflict`, execution fails with
`EngineErrorCode::UnsupportedPolicy`.

## Linux Safety and Worker Modes

### `WorkerMode::InProcess`

- Executes directly in caller context.
- Rejects `requireLandlock=true`.
- Rejects `requireSeccomp=true`.

### `WorkerMode::SandboxedThread`

- Worker always applies:
  - `PR_SET_NO_NEW_PRIVS`
  - seccomp filter install
- Landlock policy is applied optionally, and becomes mandatory only when
  `requireLandlock=true`.
- If required safety features are unavailable, error is
  `EngineErrorCode::SafetyRequirementUnavailable` with `sysErrno = ENOSYS`.

## `Result` Contract

`Result` includes:

- `success`: true only for full completion.
- `cancelled`: true when operation was cancelled/aborted.
- `finalProgress`: last monotonic aggregate progress snapshot.
- `error`: structured `Error` with:
  - `EngineErrorCode`
  - `OperationStep`
  - `sysErrno`
  - message

Cancellation always maps to `EngineErrorCode::Cancelled` with `ECANCELED`.

## Error Codes (`EngineErrorCode`)

- `None`
- `InvalidRequest`
- `UnsupportedOperation`
- `UnsupportedPolicy`
- `UnsupportedFeature`
- `SafetyRequirementUnavailable`
- `ConflictAborted`
- `OperationFailed`
- `Cancelled`

## Operation Steps (`OperationStep`)

- `None`
- `ValidateRequest`
- `BuildPlan`
- `ResolveConflict`
- `Execute`
- `Finalize`

`Finalize` exists in the enum for contract completeness; current flow reports
steps primarily in validate/build/conflict/execute paths.
