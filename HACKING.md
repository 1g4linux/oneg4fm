# Hacking Guide

This repository treats local file operations as a security-sensitive subsystem.
All semantics live in `src/core`, and adapters must not re-implement them.

## Linux Scope and Baseline

`oneg4fm` file-ops hardening is Linux-only.

Required kernel/syscall surface for hardened local operations:

- `openat2()` with `RESOLVE_*` flags for anchored no-escape resolution.
- `renameat2()` for atomic destination updates and no-replace semantics.
- `statx()` for metadata queries in the safety wrapper layer.

If a required primitive is missing, operations fail fast with an explicit
unsupported-kernel error (`ENOSYS`) and must not silently downgrade.

`O_TMPFILE` is attempted first for atomic writes, with a safe in-directory
temporary file fallback when unsupported by the filesystem.

## Security Model (File Ops)

Invariants that must hold for copy/move/delete/extract paths:

1. Resolve and mutate paths by `dirfd` + `*at` syscalls, not by reconstructed
   absolute path strings.
2. Enforce no-follow behavior for dangerous operations. Symlink traversal in
   path components is rejected.
3. Keep operations anchored under an intended root (`openat2` with
   `RESOLVE_BENEATH`, `RESOLVE_NO_SYMLINKS`, `RESOLVE_NO_MAGICLINKS`).
4. Cleanup is journal-bounded. Rollback may touch only entries created by the
   current operation.
5. Cancellation is explicit and stable: cancellation maps to `ECANCELED` and is
   surfaced in the contract result.

## Contract and Ownership Boundaries

- Canonical semantics entrypoint: `src/core/file_ops_contract.h` via
  `FileOpsContract::run`.
- Core implementation: `src/core/file_ops_contract.cpp`,
  `src/core/fs_ops.cpp`, `src/core/linux_fs_safety.cpp`.
- Qt adapter (`src/backends/qt/qt_fileops.cpp`) maps `FileOpRequest` to
  `FileOpsContract::Request` and forwards progress/conflicts/cancel only.
- `libfm-qt` compatibility bridge routes native local paths to the same core
  contract and keeps non-local/GIO paths on legacy code.

Adapters must not:

- implement alternate traversal or conflict semantics
- bypass no-follow/path anchoring policy
- add cleanup behavior outside journaled rollback

## Progress, Conflict, and Error Invariants

- Progress totals are monotonic once planned and never move backward.
- Conflict handling is centralized in the core contract; prompt-mode decisions
  flow through structured conflict events.
- Errors are structured by engine error code, operation step, and `errno`.
- User-visible cancellation is represented as `ECANCELED`.

## Where to Update Docs

- Core API reference: `docs/file-ops-core-contract.md`
- Adapter behavior (Qt + `libfm-qt` bridge): `docs/file-ops-adapters.md`

Keep these docs synchronized with implementation and tests whenever contract or
routing semantics change.
