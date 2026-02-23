# ADR 0001: D-Bus Compatibility Policy

## Status
Accepted

## Date
2026-02-23

## Context

`oneg4fm` renamed its application D-Bus ownership from legacy `org.pcmanfm.*`
to `org.oneg4fm.*`. The project needs one explicit compatibility policy so
routing behavior is predictable and testable.

## Decision

Use a **hard cut** policy with **no compatibility alias**.

- Primary service name: `org.oneg4fm.oneg4fm`
- Primary application interface: `org.oneg4fm.Application`
- No runtime or install-time alias for `org.pcmanfm.*`

## Rationale

- Prevent dual-service ambiguity in single-instance routing.
- Keep identity migration strict and measurable.
- Avoid carrying legacy names into new install artifacts.

## Enforcement

- Build constants `ONEG4FM_DBUS_APP_SERVICE` and
  `ONEG4FM_DBUS_APP_INTERFACE` are defined in
  `oneg4fm/CMakeLists.txt` and consumed in `oneg4fm/application.cpp`.
- Install artifact `oneg4fm/org.oneg4fm.oneg4fm.service.in` generates
  `org.oneg4fm.oneg4fm.service` in
  `${CMAKE_INSTALL_DATADIR}/dbus-1/services`.
- Inventory test coverage asserts ADR presence and absence of legacy
  `org.pcmanfm` references in DBus policy artifacts.

## Consequences

- Existing launchers/integrations that still target `org.pcmanfm.*` must be
  updated; no fallback alias is provided.
