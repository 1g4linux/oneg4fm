# Core File-Ops Contract

This document defines the behavior of `Oneg4FM::FileOpsContract` in
`src/core/file_ops_contract.h` and `src/core/file_ops_contract.cpp`.

## Entry Point

- API: `Oneg4FM::FileOpsContract::run(const Request&, const EventHandlers&)`
- Preflight API: `Oneg4FM::FileOpsContract::preflight(const Request&)`
- Capability API: `Oneg4FM::FileOpsContract::capabilities()`
- Backend dispatch is resolved in core and executed through one entrypoint:
  - `LocalHardened`: Linux-hardened local engine (`dirfd` + `openat2` policy)
  - `Gio`: URI/non-local and trash workflows via GIO backend executor
- Local hardened operations today: `Copy`, `Move`, `Delete`
- GIO backend operations today: `Copy`, `Move`, `Delete`, `Trash`, `Untrash`
- Rejected operations today: `Mkdir`, `Link` (`EngineErrorCode::UnsupportedOperation`)

## `Request` Fields

The contract accepts a `Request` and validates it before planning/execution.

| Field | Behavior |
| --- | --- |
| `operation` | `Copy`, `Move`, `Delete`, `Trash`, `Untrash`, `Mkdir`, `Link`. `Mkdir`/`Link` are currently rejected. `Trash`/`Untrash` require routing to backend `Gio`. |
| `sources` | Must be non-empty; each source must be non-empty and must be stat-able during plan build by the resolved backend scanner (`lstat` for local, `g_file_query_info` for GIO). |
| `destination.targetDir` | Required for `Copy`/`Move`; must be empty for `Trash`/`Untrash`. |
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
| `routing.defaultBackend` | Backend selector (`Auto`, `LocalHardened`, `Gio`) used when per-item selectors are absent. |
| `routing.sourceBackends` | Optional per-source backend selectors; if present, count must match `sources`. |
| `routing.destinationBackend` | Backend selector for copy/move destination endpoint. |
| `routing.sourceKinds` | Optional per-source endpoint kinds (`NativePath`, `Uri`); if present, count must match `sources`. |
| `routing.destinationKind` | Endpoint kind for copy/move destination (`NativePath` or `Uri`). |

The `linuxSafety.*` fields are the members of `LinuxSafetyRequirements`.
The `routing.*` fields are the members of `RoutingHints`.

Routing validation currently enforces single-backend execution per request.
Mixed backend requests are rejected with `EngineErrorCode::UnsupportedPolicy`.

For `LocalHardened` native-path execution, planned source and destination
paths are normalized by canonicalizing only their parent directories
(`realpath()` with missing-tail preservation) while keeping each leaf name
unchanged. This preserves symlink-leaf handling (`CopyLinkAsLink`) and lets
operations continue when callers provide paths through a symlinked directory
alias.

When routing resolves to `LocalHardened`, `linuxSafety.requireOpenat2Resolve`
is enforced. When routing resolves to `Gio`, local Linux hardening probes are
not required for execution.

## Conflict and Event Model

### `EventHandlers::onProgress`

- Receives `ProgressSnapshot`.
- Totals (`filesTotal`, `bytesTotal`) are planned first and then kept stable.
- Done counters are monotonic and clamped to totals.
- `currentPath` remains stable (non-empty once set).
- `phase` is monotonic: `Running` during execution, then `Finalizing` once all
  work units are complete and the operation is committing final completion.
- Successful runs end with `finalProgress.phase = Finalizing`.

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

## Backend Capabilities and Early Checks

`capabilities()` reports backend support matrix and availability:

- `localHardened`: available, supports native-path `Copy`/`Move`/`Delete`
- `gio`: available, supports native-path and URI-path `Copy`/`Move`/`Delete`
  plus `Trash`/`Untrash`

Adapters can call `preflight()` to fail early with the same structured contract
errors used by `run()`, without executing planning or mutations.

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
